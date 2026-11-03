import { timeInteraction } from '../interaction-metrics';
import { MoveType, ROLE_COUNT, readStatistics, type Header, type LayerRec, type MoveChunk, type States, type StreamMeta } from './format';
import { updateCachedRanges } from './colors';
import type { WorkerIn, WorkerOut } from './loader.worker';

export const TEX_WIDTH = 4096;

export class PreviewData {
  sourceUrl?: string;
  header: Header | null = null;
  states: States | null = null;
  n = 0;
  loaded = 0;
  done = false;
  error: string | null = null;
  bytes = 0;
  bytesTotal = 0;
  loadMs = 0;
  layerTableMatches = true;
  meta: StreamMeta = {};
  layers: LayerRec[] = [];
  layerEnd: number[] = [];
  layerTimes: number[] = [];
  colorPrintOptionLayers = new Set<number>();

  pos = new Float32Array(0);
  type = new Uint8Array(0);
  role = new Uint8Array(0);
  extruder = new Uint8Array(0);
  cp = new Uint8Array(0);
  de = new Float32Array(0);
  orientation = new Float32Array(0);
  inverseTime = new Float32Array(0);
  actual = new Float32Array(0);
  clearance = new Float32Array(0);
  feed = new Float32Array(0);
  width = new Float32Array(0);
  height = new Float32Array(0);
  mm3 = new Float32Array(0);
  time = new Float32Array(0);
  cumTime = new Float64Array(0);
  state = new Uint32Array(0);
  gcode = new Uint32Array(0);
  layer = new Uint32Array(0);
  slope: Float32Array | null = null;

  lod: Uint32Array[] = [];
  lodBlocks: number[] = [];
  lodCount = 0;

  roleTime = new Float64Array(ROLE_COUNT);
  roleFilament = new Float64Array(ROLE_COUNT);
  roleFilamentByExtruder: Float64Array[] = [];
  travelTime = 0;
  filamentMm: number[] = [];
  bbox = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  optionIds: number[] = [];

  statistics(record?: Record<string, unknown>) {
    return readStatistics(record?.statistics) ?? this.meta.statistics;
  }

  get rows() {
    return Math.max(1, Math.ceil(this.n / TEX_WIDTH));
  }

  onHeader(h: Header, states: States, total: number) {
    this.header = h;
    this.states = states;
    this.n = h.moveCount;
    this.bytesTotal = total;
    const n = h.moveCount;
    this.pos = new Float32Array(this.rows * TEX_WIDTH * 4);
    this.type = new Uint8Array(n);
    this.role = new Uint8Array(n);
    this.extruder = new Uint8Array(n);
    this.cp = new Uint8Array(n);
    this.de = new Float32Array(n);
    this.orientation = new Float32Array(h.oriented ? n * 3 : 0);
    this.inverseTime = new Float32Array(h.oriented ? n : 0);
    this.actual = new Float32Array(h.extended ? n : 0);
    this.clearance = new Float32Array(h.extended ? n : 0);
    this.feed = new Float32Array(n);
    this.width = new Float32Array(n);
    this.height = new Float32Array(n);
    this.mm3 = new Float32Array(n);
    this.time = new Float32Array(n);
    this.cumTime = new Float64Array(n);
    this.state = new Uint32Array(n);
    this.gcode = new Uint32Array(n);
    this.layer = new Uint32Array(n);
  }

  onChunk(c: MoveChunk, layers: LayerRec[], lod: Uint32Array, lodBlocks: Uint32Array) {
    const finishTiming = timeInteraction('preview-chunk');
    const s = c.start;
    this.pos.set(c.pos, s * 4);
    this.type.set(c.type, s);
    this.role.set(c.role, s);
    this.extruder.set(c.extruder, s);
    this.cp.set(c.cp, s);
    this.de.set(c.de, s);
    if (c.orientation.length) { this.orientation.set(c.orientation, s * 3); this.inverseTime.set(c.inverseTime, s); }
    if (c.actual.length) {
      this.actual.set(c.actual, s);
      this.clearance.set(c.clearance, s);
    }
    this.feed.set(c.feed, s);
    this.width.set(c.width, s);
    this.height.set(c.height, s);
    this.mm3.set(c.mm3, s);
    this.time.set(c.time, s);
    this.state.set(c.state, s);
    this.gcode.set(c.gcode, s);
    this.layer.set(c.layer, s);
    this.loaded = s + c.count;
    for (let i = 0; i < lodBlocks.length; ++i) this.lodBlocks.push(this.lodCount + lodBlocks[i]);
    this.lod.push(lod);
    this.lodCount += lod.length / 2;
    this.setLayers(layers);
    for (let i = Math.max(1, s); i < this.loaded; ++i) {
      this.cumTime[i] = this.cumTime[i - 1] + this.time[i];
      const t = this.type[i] & 0x7f;
      const lt = this.layer[i];
      this.layerTimes[lt] = (this.layerTimes[lt] ?? 0) + this.time[i];
      if (t === MoveType.Extrude) {
        this.roleTime[this.role[i]] += this.time[i];
        if (!(this.type[i] & 0x80)) {
          this.roleFilament[this.role[i]] += this.de[i];
          const e = this.extruder[i];
          const roles = this.roleFilamentByExtruder[e] ??= new Float64Array(ROLE_COUNT);
          roles[this.role[i]] += this.de[i];
          this.filamentMm[e] = (this.filamentMm[e] ?? 0) + this.de[i];
        }
        for (let k = 0; k < 3; ++k) {
          const v = this.pos[i * 4 + k];
          if (v < this.bbox.min[k]) this.bbox.min[k] = v;
          if (v > this.bbox.max[k]) this.bbox.max[k] = v;
        }
      } else if (t === MoveType.Travel) this.travelTime += this.time[i];
      else if (t >= MoveType.Retract && t <= MoveType.CustomGCode) {
        this.optionIds.push(i);
        if (t === MoveType.PausePrint || t === MoveType.CustomGCode) this.colorPrintOptionLayers.add(lt);
      }
    }
    updateCachedRanges(this);
    finishTiming();
  }

  setLayers(layers: LayerRec[]) {
    this.layers = layers;
    this.layerEnd = layers.map((_, i) => (i + 1 < layers.length ? layers[i + 1].first : this.loaded));
  }

  layerSpan(lo: number, hi: number): [number, number] {
    if (!this.layers.length) return [1, 1];
    return [this.layers[lo].first, this.layerEnd[hi]];
  }

  totalTime(mode = 0): number {
    const t = this.meta.estimated_times?.[mode]?.time;
    if (typeof t === 'number' && Number.isFinite(t)) return t;
    let sum = 0;
    for (const v of this.layerTimes) sum += v ?? 0;
    return sum;
  }

  computeSlope() {
    if (this.slope) return;
    const st = this.states!;
    const out = new Float32Array(this.n);
    const CELL = 0.5;
    const groups = new Map<string, number[]>();
    for (let i = 1; i < this.loaded; ++i) {
      if ((this.type[i] & 0x7f) !== MoveType.Extrude) continue;
      const s = this.state[i];
      if (st.shell[s] === 0xff) continue;
      const key = `${st.label[s]}|${st.patch[s]}|${st.shell[s]}`;
      let g = groups.get(key);
      if (!g) groups.set(key, (g = []));
      g.push(i);
    }
    for (const ids of groups.values()) {
      const grid = new Map<number, [number, number]>();
      const cell = (x: number, y: number) => Math.round(x / CELL) * 100003 + Math.round(y / CELL);
      for (const i of ids) {
        const k = cell(this.pos[i * 4], this.pos[i * 4 + 1]);
        const c = grid.get(k);
        if (c) { c[0] += this.pos[i * 4 + 2]; c[1]++; } else grid.set(k, [this.pos[i * 4 + 2], 1]);
      }
      const z = (cx: number, cy: number) => {
        const c = grid.get(cx * 100003 + cy);
        return c ? c[0] / c[1] : NaN;
      };
      for (const i of ids) {
        const cx = Math.round(this.pos[i * 4] / CELL), cy = Math.round(this.pos[i * 4 + 1] / CELL);
        let gx = NaN, gy = NaN;
        for (let r = 1; r <= 3 && (Number.isNaN(gx) || Number.isNaN(gy)); ++r) {
          if (Number.isNaN(gx)) {
            const a = z(cx + r, cy), b = z(cx - r, cy), c0 = z(cx, cy);
            gx = !Number.isNaN(a) && !Number.isNaN(b) ? (a - b) / (2 * r * CELL) : !Number.isNaN(a) ? (a - c0) / (r * CELL) : !Number.isNaN(b) ? (c0 - b) / (r * CELL) : NaN;
          }
          if (Number.isNaN(gy)) {
            const a = z(cx, cy + r), b = z(cx, cy - r), c0 = z(cx, cy);
            gy = !Number.isNaN(a) && !Number.isNaN(b) ? (a - b) / (2 * r * CELL) : !Number.isNaN(a) ? (a - c0) / (r * CELL) : !Number.isNaN(b) ? (c0 - b) / (r * CELL) : NaN;
          }
        }
        const g = Math.hypot(Number.isNaN(gx) ? 0 : gx, Number.isNaN(gy) ? 0 : gy);
        out[i] = (Math.atan(g) * 180) / Math.PI;
      }
    }
    this.slope = out;
  }
}

export interface LoadHandlers {
  header(d: PreviewData): void;
  chunk(d: PreviewData, start: number, count: number): void;
  done(d: PreviewData): void;
  error(d: PreviewData, message: string): void;
}

export function supportGrams(d: PreviewData | null, total: number | null): number | null {
  if (!d?.done) return null;
  const mm = d.roleFilament[14] + d.roleFilament[15] + d.roleFilament[16];
  const all = d.filamentMm.reduce((a, b) => a + (b ?? 0), 0);
  return mm <= 0 ? 0 : total != null && all > 0 ? (total * mm) / all : null;
}

export function loadMoves(src: WorkerIn, on: LoadHandlers, colours?: string[]): { data: PreviewData; cancel(): void } {
  const d = new PreviewData();
  d.sourceUrl = src.url?.split('?')[0];
  let worker: Worker;
  try {
    worker = new Worker(new URL('./loader.worker.ts', import.meta.url), { type: 'module' });
  } catch (err) {
    d.error = String(err instanceof Error ? err.message : err);
    queueMicrotask(() => on.error(d, d.error!));
    return { data: d, cancel() {} };
  }
  const fail = (message: string) => {
    if (d.error) return;
    d.error = message;
    worker.terminate();
    on.error(d, message);
  };
  worker.onerror = (e) => { e.preventDefault(); fail(e.message || 'Preview worker stopped'); };
  worker.onmessageerror = () => fail('Preview data could not be transferred');
  worker.onmessage = (e: MessageEvent<WorkerOut>) => {
    try {
      const m = e.data;
      if (m.kind === 'header') {
        d.onHeader(m.header, m.states, m.bytesTotal);
        on.header(d);
      } else if (m.kind === 'chunk') {
        d.onChunk(m.chunk, m.layers, m.lod, m.lodBlocks);
        d.bytes = m.bytes;
        on.chunk(d, m.chunk.start, m.chunk.count);
        let ready = false;
        const ack = () => { if (!ready) { ready = true; worker.postMessage({ kind: 'ready' }); } };
        const timer = setTimeout(ack, 32);
        requestAnimationFrame(() => { clearTimeout(timer); ack(); });
      } else if (m.kind === 'done') {
        d.meta = colours ? { ...m.meta, extruder_colors: colours } : m.meta;
        d.setLayers(m.layers);
        d.loadMs = m.ms;
        d.bytes = m.bytes;
        d.layerTableMatches = m.layerTableMatches;
        d.done = true;
        worker.terminate();
        on.done(d);
      } else {
        fail(m.message);
      }
    } catch (err) { fail(String(err instanceof Error ? err.message : err)); }
  };
  try { worker.postMessage(src); } catch (err) {
    queueMicrotask(() => fail(String(err instanceof Error ? err.message : err)));
  }
  return { data: d, cancel: () => worker.terminate() };
}
