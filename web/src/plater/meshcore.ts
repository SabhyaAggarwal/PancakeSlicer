


import { Vector3 } from 'three';
import { ConvexHull } from 'three/examples/jsm/math/ConvexHull.js';


export function parseStl(buf: ArrayBuffer): Float32Array {
  const dv = new DataView(buf);
  const tris = buf.byteLength >= 84 ? dv.getUint32(80, true) : -1;
  if (tris >= 0 && 84 + tris * 50 === buf.byteLength) {
    const out = new Float32Array(tris * 9);
    for (let t = 0, o = 0; t < tris; t++) {
      const base = 84 + t * 50 + 12;
      for (let k = 0; k < 9; k++) out[o++] = dv.getFloat32(base + 4 * k, true);
    }
    return out;
  }
  const text = new TextDecoder().decode(buf);
  const out: number[] = [];
  const re = /vertex\s+(\S+)\s+(\S+)\s+(\S+)/g;
  for (let m; (m = re.exec(text)); ) out.push(+m[1], +m[2], +m[3]);
  if (!out.length) throw new Error('no triangles in the STL');
  return new Float32Array(out);
}


export function parseObj(text: string): Float32Array {
  const v: number[] = [];
  const out: number[] = [];
  const idx = (s: string) => {
    const i = parseInt(s, 10);
    return i < 0 ? v.length / 3 + i : i - 1;
  };
  const push = (i: number) => out.push(v[3 * i], v[3 * i + 1], v[3 * i + 2]);
  for (const line of text.split('\n')) {
    if (line.startsWith('v ')) {
      const p = line.trim().split(/\s+/);
      v.push(+p[1], +p[2], +p[3]);
    } else if (line.startsWith('f ')) {
      const p = line.trim().split(/\s+/).slice(1).map(idx);
      for (let k = 1; k + 1 < p.length; k++) push(p[0]), push(p[k]), push(p[k + 1]);
    }
  }
  if (!out.length) throw new Error('no mesh in the OBJ');
  return new Float32Array(out);
}






export function weld(soup: Float32Array): { positions: Float32Array; index: Uint32Array } {
  const n = soup.length / 3;
  let size = 1;
  while (size < n * 1.5) size <<= 1;
  const mask = size - 1;
  const table = new Int32Array(size).fill(-1);
  const keys = new Int32Array(n * 3);
  const positions = new Float32Array(n * 3);
  const index = new Uint32Array(n);
  let unique = 0;
  for (let i = 0; i < n; i++) {
    const x = soup[3 * i], y = soup[3 * i + 1], z = soup[3 * i + 2];
    const a = Math.trunc(x * 1e4 + 0.5), b = Math.trunc(y * 1e4 + 0.5), c = Math.trunc(z * 1e4 + 0.5);
    let h = (Math.imul(a, 73856093) ^ Math.imul(b, 19349663) ^ Math.imul(c, 83492791)) & mask;
    for (;;) {
      const j = table[h];
      if (j < 0) {
        table[h] = unique;
        keys[3 * unique] = a, keys[3 * unique + 1] = b, keys[3 * unique + 2] = c;
        positions[3 * unique] = x, positions[3 * unique + 1] = y, positions[3 * unique + 2] = z;
        index[i] = unique++;
        break;
      }
      if (keys[3 * j] === a && keys[3 * j + 1] === b && keys[3 * j + 2] === c) {
        index[i] = j;
        break;
      }
      h = (h + 1) & mask;
    }
  }
  return { positions: positions.slice(0, unique * 3), index };
}


export function vertexNormals(p: Float32Array, index: Uint32Array): Float32Array {
  const nrm = new Float32Array(p.length);
  for (let t = 0; t < index.length; t += 3) {
    const a = 3 * index[t], b = 3 * index[t + 1], c = 3 * index[t + 2];
    const cbx = p[c] - p[b], cby = p[c + 1] - p[b + 1], cbz = p[c + 2] - p[b + 2];
    const abx = p[a] - p[b], aby = p[a + 1] - p[b + 1], abz = p[a + 2] - p[b + 2];
    const nx = cby * abz - cbz * aby, ny = cbz * abx - cbx * abz, nz = cbx * aby - cby * abx;
    for (const v of [a, b, c]) nrm[v] += nx, nrm[v + 1] += ny, nrm[v + 2] += nz;
  }
  for (let i = 0; i < nrm.length; i += 3) {
    const l = Math.hypot(nrm[i], nrm[i + 1], nrm[i + 2]) || 1;
    nrm[i] /= l, nrm[i + 1] /= l, nrm[i + 2] /= l;
  }
  return nrm;
}


export function centre(p: Float32Array): { centre: [number, number, number]; min: [number, number, number]; max: [number, number, number] } {
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < p.length; i += 3)
    for (let k = 0; k < 3; k++) {
      const v = p[i + k];
      if (v < min[k]) min[k] = v;
      if (v > max[k]) max[k] = v;
    }
  const c: [number, number, number] = [(min[0] + max[0]) / 2, (min[1] + max[1]) / 2, min[2]];
  for (let i = 0; i < p.length; i += 3) p[i] -= c[0], p[i + 1] -= c[1], p[i + 2] -= c[2];
  return { centre: c, min: [min[0] - c[0], min[1] - c[1], 0], max: [max[0] - c[0], max[1] - c[1], max[2] - c[2]] };
}







export function hullPoints(p: Float32Array): Float32Array {
  const n = p.length / 3;
  if (n <= 2048) return p.slice();
  const dirs: number[][] = [];
  for (let x = -1; x <= 1; x++) for (let y = -1; y <= 1; y++) for (let z = -1; z <= 1; z++) if (x || y || z) dirs.push([x, y, z]);
  const best = dirs.map(() => -Infinity), arg = dirs.map(() => 0);
  for (let i = 0; i < n; i++) {
    const x = p[3 * i], y = p[3 * i + 1], z = p[3 * i + 2];
    for (let d = 0; d < dirs.length; d++) {
      const s = dirs[d][0] * x + dirs[d][1] * y + dirs[d][2] * z;
      if (s > best[d]) best[d] = s, arg[d] = i;
    }
  }
  const vec = (i: number) => new Vector3(p[3 * i], p[3 * i + 1], p[3 * i + 2]);
  const inner = new ConvexHull().setFromPoints([...new Set(arg)].map(vec));
  const planes = inner.faces.map((f) => [f.normal.x, f.normal.y, f.normal.z, f.constant]);
  const keep: Vector3[] = [];
  const eps = 1e-6;
  for (let i = 0; i < n; i++) {
    const x = p[3 * i], y = p[3 * i + 1], z = p[3 * i + 2];
    let outside = planes.length < 4;
    for (let f = 0; f < planes.length && !outside; f++) outside = planes[f][0] * x + planes[f][1] * y + planes[f][2] * z - planes[f][3] > -eps;
    if (outside) keep.push(new Vector3(x, y, z));
  }
  const hull = new ConvexHull().setFromPoints(keep);
  const verts = new Set<Vector3>();
  for (const f of hull.faces) {
    let e = f.edge;
    do {
      verts.add(e.head().point);
      e = e.next;
    } while (e !== f.edge);
  }
  if (verts.size < 4) return p.slice();
  const out = new Float32Array(verts.size * 3);
  let o = 0;
  for (const v of verts) out[o++] = v.x, out[o++] = v.y, out[o++] = v.z;
  return out;
}







export function bakeStl(p: Float32Array, index: Uint32Array | Uint16Array | null, m: ArrayLike<number>): Blob {
  const tp = new Float64Array(p.length);
  for (let i = 0; i < p.length; i += 3) {
    const x = p[i], y = p[i + 1], z = p[i + 2];
    tp[i] = m[0] * x + m[4] * y + m[8] * z + m[12];
    tp[i + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
    tp[i + 2] = m[2] * x + m[6] * y + m[10] * z + m[14];
  }
  const tris = (index ? index.length : p.length / 3) / 3;

  const flip = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]) < 0;
  const head = new ArrayBuffer(84);
  new TextEncoder().encodeInto('PancakeSlicer web plate object', new Uint8Array(head, 0, 80));
  new DataView(head).setUint32(80, tris, true);
  const parts: ArrayBuffer[] = [head];
  const PER = 20971;
  const rec = new Float32Array(13);
  const recBytes = new Uint8Array(rec.buffer, 0, 50);
  for (let t0 = 0; t0 < tris; t0 += PER) {
    const count = Math.min(PER, tris - t0);
    const out = new Uint8Array(count * 50);
    for (let t = t0, off = 0; t < t0 + count; t++, off += 50) {
      const a = 3 * (index ? index[3 * t] : 3 * t);
      let b = 3 * (index ? index[3 * t + 1] : 3 * t + 1), c = 3 * (index ? index[3 * t + 2] : 3 * t + 2);
      if (flip) [b, c] = [c, b];

      const cbx = tp[c] - tp[b], cby = tp[c + 1] - tp[b + 1], cbz = tp[c + 2] - tp[b + 2];
      const abx = tp[a] - tp[b], aby = tp[a + 1] - tp[b + 1], abz = tp[a + 2] - tp[b + 2];
      const nx = cby * abz - cbz * aby, ny = cbz * abx - cbx * abz, nz = cbx * aby - cby * abx;
      const l = Math.hypot(nx, ny, nz) || 1;
      rec[0] = nx / l, rec[1] = ny / l, rec[2] = nz / l;
      rec[3] = tp[a], rec[4] = tp[a + 1], rec[5] = tp[a + 2];
      rec[6] = tp[b], rec[7] = tp[b + 1], rec[8] = tp[b + 2];
      rec[9] = tp[c], rec[10] = tp[c + 1], rec[11] = tp[c + 2];
      out.set(recBytes, off);
    }
    parts.push(out.buffer);
  }
  return new Blob(parts, { type: 'model/stl' });
}
