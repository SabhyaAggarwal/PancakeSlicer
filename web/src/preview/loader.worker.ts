import { unzipSync } from 'fflate';
import { gcodeToStream } from './gcode';
import { BLOCK, CHUNK, MAGIC, TRANSFER_MAGIC, readHeader, readStates, readStatistics, MoveType, type Header, type LayerRec, type MoveChunk, type States, type StreamMeta } from './format';

const LOD_TOLERANCE = 0.05;
const CUSTOM_ROLE = 18;

export type WorkerIn = { url?: string; file?: Blob };
export type WorkerOut =
  | { kind: 'header'; header: Header; states: States; bytesTotal: number }
  | { kind: 'chunk'; chunk: MoveChunk; layers: LayerRec[]; lod: Uint32Array; lodBlocks: Uint32Array; bytes: number }
  | { kind: 'done'; layers: LayerRec[]; meta: StreamMeta; ms: number; bytes: number; layerTableMatches: boolean }
  | { kind: 'error'; message: string };

const post = (m: WorkerOut, transfer: Transferable[] = []) => (self as unknown as Worker).postMessage(m, transfer);

async function open(src: WorkerIn): Promise<{ stream: ReadableStream<Uint8Array>; total: number }> {
  let body: ReadableStream<Uint8Array>;
  let total = 0;
  if (src.file) {
    const peek = new Uint8Array(await src.file.slice(0, 8).arrayBuffer());
    const magic = String.fromCharCode(...peek);
    if (magic.startsWith('PK') || (magic !== MAGIC && magic !== TRANSFER_MAGIC && !(peek[0] === 0x1f && peek[1] === 0x8b))) {
      let text: string;
      if (magic.startsWith('PK')) {
        const zip = unzipSync(new Uint8Array(await src.file.arrayBuffer()), { filter: (f) => /^Metadata\/plate_\d+\.gcode$/.test(f.name) });
        const name = Object.keys(zip).sort()[0];
        if (!name) throw new Error('no plate G-code in this 3MF');
        text = new TextDecoder().decode(zip[name]);
      } else text = await src.file.text();
      const stream = new Blob([gcodeToStream(text)]);
      return { stream: stream.stream() as ReadableStream<Uint8Array>, total: stream.size };
    }
    body = src.file.stream() as ReadableStream<Uint8Array>;
    total = src.file.size;
  } else {
    const r = await fetch(src.url!);
    if (!r.ok || !r.body) throw new Error(`${r.status} ${r.statusText} for ${src.url}`);
    body = r.body;
    if (!r.headers.get('content-encoding')) total = Number(r.headers.get('content-length') ?? 0);
  }
  const reader = body.getReader();
  const first = await reader.read();
  const head = first.value ?? new Uint8Array();
  const rest = new ReadableStream<Uint8Array>({
    start(c) {
      if (head.length) c.enqueue(head);
      if (first.done) c.close();
    },
    async pull(c) {
      const { value, done } = await reader.read();
      if (done) c.close();
      else c.enqueue(value);
    },
  });
  if (head[0] === 0x1f && head[1] === 0x8b) {
    if (typeof DecompressionStream === 'undefined')
      throw new Error('Unsupported browser: gzip preview requires Chrome or Edge 109+, Firefox 115+, or Safari 16.4+.');
    return { stream: rest.pipeThrough(new DecompressionStream('gzip') as unknown as TransformStream<Uint8Array, Uint8Array>), total: 0 };
  }
  return { stream: rest, total };
}

class Bytes {
  buf = new Uint8Array(1 << 20);
  len = 0;
  pos = 0;
  push(b: Uint8Array) {
    if (this.len + b.length > this.buf.length) {
      const keep = this.len - this.pos;
      const cap = Math.max(this.buf.length, (keep + b.length) * 2);
      if (cap > this.buf.length) {
        const next = new Uint8Array(cap);
        next.set(this.buf.subarray(this.pos, this.len), 0);
        this.buf = next;
      } else this.buf.copyWithin(0, this.pos, this.len);
      this.len = keep;
      this.pos = 0;
    }
    this.buf.set(b, this.len);
    this.len += b.length;
  }
  get avail() {
    return this.len - this.pos;
  }
  view(n: number) {
    return new DataView(this.buf.buffer, this.buf.byteOffset + this.pos, n);
  }
}

function unplane(q: Bytes, n: number, stride: number) {
  const src = q.buf.slice(q.pos, q.pos + n * stride);
  const out = q.view(n * stride);
  for (let w = 0, at = 0; w < stride / 4; ++w, at += 4 * n) {
    let v = 0;
    for (let i = 0; i < n; ++i) {
      v = (v + (src[at + i] | src[at + n + i] << 8 | src[at + 2 * n + i] << 16 | src[at + 3 * n + i] << 24)) >>> 0;
      out.setUint32(i * stride + w * 4, v, true);
    }
  }
}

function allocChunk(start: number, count: number, extended: boolean, oriented: boolean): MoveChunk {
  const ext = extended ? count : 0;
  return {
    orientation: new Float32Array(oriented ? count * 3 : 0), inverseTime: new Float32Array(oriented ? count : 0),
    actual: new Float32Array(ext), clearance: new Float32Array(ext),
    start, count, pos: new Float32Array(count * 4), type: new Uint8Array(count), role: new Uint8Array(count),
    extruder: new Uint8Array(count), cp: new Uint8Array(count), de: new Float32Array(count), feed: new Float32Array(count),
    width: new Float32Array(count), height: new Float32Array(count), mm3: new Float32Array(count),
    time: new Float32Array(count), state: new Uint32Array(count), gcode: new Uint32Array(count), layer: new Uint32Array(count),
  };
}

const near = (a: number, b: number) => Math.abs(a - b) <= 0.05 * Math.max(Math.abs(a), Math.abs(b));
const sameClearance = (a: number, b: number) => (Number.isNaN(a) ? Number.isNaN(b) : Math.abs(a - b) <= 0.05);

function buildLod(c: MoveChunk, prev: [number, number, number]): { lod: Uint32Array; blocks: Uint32Array } {
  const out: number[] = [];
  const blocks = new Uint32Array(Math.ceil(c.count / BLOCK) * 2);
  const P = c.pos;
  const px = (v: number, k: number) => (v < c.start ? prev[k] : P[(v - c.start) * 4 + k]);
  const keep = new Uint8Array(c.count + 1);
  const stack: number[] = [];
  const dp = (a: number, b: number) => {
    stack.push(a, b);
    while (stack.length) {
      const e = stack.pop()!, s = stack.pop()!;
      if (e - s < 2) continue;
      const ax = px(s, 0), ay = px(s, 1), az = px(s, 2);
      const dx = px(e, 0) - ax, dy = px(e, 1) - ay, dz = px(e, 2) - az;
      const l2 = dx * dx + dy * dy + dz * dz;
      let worst = -1, wd = LOD_TOLERANCE * LOD_TOLERANCE;
      for (let v = s + 1; v < e; ++v) {
        const qx = px(v, 0) - ax, qy = px(v, 1) - ay, qz = px(v, 2) - az;
        let t = l2 > 0 ? (qx * dx + qy * dy + qz * dz) / l2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const ex = qx - t * dx, ey = qy - t * dy, ez = qz - t * dz;
        const d = ex * ex + ey * ey + ez * ez;
        if (d > wd) { wd = d; worst = v; }
      }
      if (worst >= 0) {
        keep[worst - c.start + 1] = 1;
        stack.push(s, worst, worst, e);
      }
    }
  };
  for (let b0 = 0, bi = 0; b0 < c.count; b0 += BLOCK, ++bi) {
    const b1 = Math.min(c.count, b0 + BLOCK);
    blocks[bi * 2] = out.length / 2;
    let i = b0;
    while (i < b1) {
      const t = c.type[i] & 0x7f;
      if ((t !== MoveType.Extrude && t !== MoveType.Travel && t !== MoveType.Wipe) || c.start + i === 1) { ++i; continue; }
      let j = i + 1;
      while (j < b1 && (c.type[j] & 0x7f) === t && c.role[j] === c.role[i] && c.extruder[j] === c.extruder[i] &&
             c.cp[j] === c.cp[i] && (!c.orientation.length || [0, 1, 2].every((k) => Math.abs(c.orientation[j * 3 + k] - c.orientation[i * 3 + k]) <= 0.1)) && c.layer[j] === c.layer[i] && c.state[j] === c.state[i] && c.feed[j] === c.feed[i] &&
             near(c.width[j], c.width[i]) && near(c.height[j], c.height[i]) && near(c.mm3[j], c.mm3[i]) &&
             (!c.actual.length || near(c.actual[j], c.actual[i]) && sameClearance(c.clearance[j], c.clearance[i]))) ++j;
      const va = c.start + i - 1, vb = c.start + j - 1;
      keep[va - c.start + 1] = 1;
      keep[vb - c.start + 1] = 1;
      dp(va, vb);
      let last = va;
      for (let v = va + 1; v <= vb; ++v)
        if (keep[v - c.start + 1]) {
          out.push(last, v);
          last = v;
        }
      i = j;
    }
    blocks[bi * 2 + 1] = out.length / 2;
  }
  return { lod: Uint32Array.from(out), blocks };
}

async function run(src: WorkerIn) {
  const t0 = performance.now();
  const { stream, total } = await open(src);
  const reader = stream.getReader();
  const q = new Bytes();
  let received = 0;
  let header: Header | null = null;
  let states: States | null = null;
  let parsed = 0;
  let stage: 'header' | 'states' | 'moves' | 'layers' | 'meta' | 'end' = 'header';
  const layers: LayerRec[] = [];
  const fileLayers: LayerRec[] = [];
  const vgcodeZ: number[] = [];
  let meta: StreamMeta = {};
  let prev: [number, number, number] = [0, 0, 0];
  let eof = false;

  const flushMoves = async (final: boolean) => {
    const h = header!;
    while (parsed < h.moveCount) {
      const n = Math.min(CHUNK, h.moveCount - parsed);
      if (q.avail < n * h.moveStride) {
        if (final) throw new Error('truncated move stream (moves)');
        return;
      }
      if (h.block) unplane(q, n, h.moveStride);
      const c = allocChunk(parsed, n, h.extended, !!h.oriented);
      const dv = q.view(n * h.moveStride);
      for (let i = 0, o = 0; i < n; ++i, o += h.moveStride) {
        const v = parsed + i;
        c.gcode[i] = dv.getUint32(o, true);
        const type = dv.getUint8(o + 4);
        c.type[i] = type;
        c.role[i] = dv.getUint8(o + 5);
        c.extruder[i] = dv.getUint8(o + 6);
        c.cp[i] = dv.getUint8(o + 7);
        c.pos[i * 4] = dv.getFloat32(o + 8, true);
        c.pos[i * 4 + 1] = dv.getFloat32(o + 12, true);
        c.pos[i * 4 + 2] = dv.getFloat32(o + 16, true);
        c.de[i] = dv.getFloat32(o + 20, true);
        c.feed[i] = dv.getFloat32(o + 24, true);
        c.width[i] = dv.getFloat32(o + 28, true);
        c.height[i] = dv.getFloat32(o + 32, true);
        c.mm3[i] = dv.getFloat32(o + 36, true);
        c.time[i] = dv.getFloat32(o + 40, true);
        if (h.extended) {
          c.actual[i] = dv.getFloat32(o + 52, true);
          c.clearance[i] = dv.getFloat32(o + 56, true);
        }
        if (h.oriented) {
          const at = o + h.moveStride - 16;
          for (let k = 0; k < 3; ++k) c.orientation[i * 3 + k] = dv.getFloat32(at + k * 4, true);
          c.inverseTime[i] = dv.getFloat32(at + 12, true);
        }
        const layerId = dv.getUint32(o + 44, true);
        let st = dv.getUint32(o + 48, true);
        if (st >= h.stateCount) st = 0;
        c.state[i] = st;
        if (v === 0) {
          c.layer[i] = 0;
          continue;
        }
        let z = states!.printZ[st] ?? 0;
        if (z <= 0 && (type & 0x7f) === MoveType.Extrude && states!.shell[st] === 0xff) z = c.pos[i * 4 + 2];
        const last = layers[layers.length - 1];
        const extrusionZ = (type & 0x7f) === MoveType.Extrude && c.role[i] !== CUSTOM_ROLE ? c.pos[i * 4 + 2] : null;
        if (!last || last.id !== layerId) {
          layers.push({ id: layerId, first: v, z });
          vgcodeZ.push(extrusionZ ?? 0);
        } else {
          if (z > last.z) last.z = z;
          if (extrusionZ != null) vgcodeZ[vgcodeZ.length - 1] = extrusionZ;
        }
        c.layer[i] = layers.length - 1;
      }
      q.pos += n * h.moveStride;
      const { lod, blocks } = buildLod(c, prev);
      prev = [c.pos[(n - 1) * 4], c.pos[(n - 1) * 4 + 1], c.pos[(n - 1) * 4 + 2]];
      parsed += n;
      const buffers = [c.orientation, c.inverseTime, c.pos, c.type, c.role, c.extruder, c.cp, c.de, c.actual, c.clearance, c.feed, c.width, c.height, c.mm3, c.time, c.state, c.gcode, c.layer, lod, blocks]
        .map((a) => a.buffer as ArrayBuffer);
      const ready = new Promise<void>((resolve) => { resume = resolve; });
      post({ kind: 'chunk', chunk: c, layers: layers.map((l) => ({ ...l })), lod, lodBlocks: blocks, bytes: received }, buffers);
      await ready;
    }
  };

  for (;;) {
    if (!eof) {
      const { value, done } = await reader.read();
      if (done) eof = true;
      else {
        q.push(value);
        received += value.length;
      }
    }
    if (stage === 'header' && q.avail >= 64) {
      const dv0 = q.view(64);
      const h = readHeader(dv0);
      if (q.avail < h.headerSize) { if (eof) throw new Error('truncated header'); continue; }
      header = h;
      q.pos += h.headerSize;
      stage = 'states';
    }
    if (stage === 'states' && header && q.avail >= header.stateCount * header.stateStride) {
      states = readStates(q.view(header.stateCount * header.stateStride), 0, header);
      q.pos += header.stateCount * header.stateStride;
      post({ kind: 'header', header, states, bytesTotal: total });
      stage = 'moves';
    }
    if (stage === 'moves') {
      await flushMoves(eof);
      if (parsed === header!.moveCount) stage = 'layers';
    }
    if (stage === 'layers' && q.avail >= header!.layerCount * header!.layerStride) {
      const h = header!;
      const dv = q.view(h.layerCount * h.layerStride);
      for (let i = 0, o = 0; i < h.layerCount; ++i, o += h.layerStride)
        fileLayers.push({ id: dv.getUint32(o, true), first: Number(dv.getBigUint64(o + 4, true)), z: dv.getFloat32(o + 12, true) });
      q.pos += h.layerCount * h.layerStride;
      stage = 'meta';
    }
    if (stage === 'meta' && q.avail >= header!.metaBytes) {
      const bytes = q.buf.slice(q.pos, q.pos + header!.metaBytes);
      try {
        const value = JSON.parse(new TextDecoder().decode(bytes));
        if (value && typeof value === 'object' && !Array.isArray(value)) {
          meta = value as StreamMeta;
          meta.statistics = readStatistics(meta.statistics);
        }
      } catch {
        meta = {};
      }
      stage = 'end';
    }
    if (stage === 'end') break;
    if (eof) {
      if (stage === 'moves' || stage === 'header' || stage === 'states') throw new Error(`truncated move stream (${stage})`);
      break;
    }
  }
  const matches = fileLayers.length === layers.length &&
    fileLayers.every((l, i) => l.id === layers[i].id && l.first === layers[i].first);
  const final = matches ? fileLayers : layers;
  if (!meta.belt_tilt_angle && final.some((l, i) => i > 0 && final[i - 1].z >= l.z))
    final.forEach((l, i) => (l.z = vgcodeZ[i]));
  post({ kind: 'done', layers: final, meta, ms: performance.now() - t0, bytes: received, layerTableMatches: matches });
}

let resume: (() => void) | null = null;
self.onmessage = (e: MessageEvent<WorkerIn | { kind: 'ready' }>) => {
  if ('kind' in e.data) { const next = resume; resume = null; next?.(); return; }
  run(e.data).catch((err) => post({ kind: 'error', message: String(err?.message ?? err) }));
};
