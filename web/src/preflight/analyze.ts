import { buildBvh, raycastBvh } from '../plater/bvh';

export interface AnalyzeIn {
  positions: Float32Array;
  index: Uint32Array;
  matrix: ArrayLike<number>;
  firstLayer: number;
  threshold: number;
  nozzle: number;
}

export interface Region {
  tris: Uint32Array;
  area: number;
  reach: number;
  bridge: boolean;
  span: number;
  z: number;
}

export interface Analysis {
  height: number;
  contact: number;
  foot: Uint32Array;
  overhangs: Region[];
  thin: { tris: Uint32Array; mm: Float32Array; area: Float32Array };
  slender: { ratio: number; width: number; z: number };
  sway: Uint32Array;
  ms: number;
}

function section(W: Float32Array, index: Uint32Array, N: Float32Array, z: number): { area: number; width: number } {
  const dirs = [[1, 0], [0, 1], [Math.SQRT1_2, Math.SQRT1_2], [Math.SQRT1_2, -Math.SQRT1_2]];
  const lo = [Infinity, Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity, -Infinity];
  let area = 0;
  const pts: number[] = [];
  for (let t = 0, T = index.length / 3; t < T; t++) {
    pts.length = 0;
    for (let e = 0; e < 3; e++) {
      const a = 3 * index[3 * t + e], b = 3 * index[3 * t + ((e + 1) % 3)];
      const za = W[a + 2] - z, zb = W[b + 2] - z;
      if ((za < 0) === (zb < 0)) continue;
      const s = za / (za - zb);
      pts.push(W[a] + s * (W[b] - W[a]), W[a + 1] + s * (W[b + 1] - W[a + 1]));
    }
    if (pts.length !== 4) continue;
    let [px, py, qx, qy] = pts;
    if ((qx - px) * -N[3 * t + 1] + (qy - py) * N[3 * t] < 0) [px, py, qx, qy] = [qx, qy, px, py];
    area += (px * qy - qx * py) / 2;
    for (let d = 0; d < 4; d++) {
      const u = px * dirs[d][0] + py * dirs[d][1], v = qx * dirs[d][0] + qy * dirs[d][1];
      lo[d] = Math.min(lo[d], u, v), hi[d] = Math.max(hi[d], u, v);
    }
  }
  return { area: Math.max(0, area), width: area > 0 ? Math.min(...hi.map((h, d) => h - lo[d])) : 0 };
}

class Grid {
  private cells = new Map<number, number[]>();
  private cell: number;
  constructor(cell: number) { this.cell = cell; }
  private key = (i: number, j: number) => (i + 32768) * 65536 + (j + 32768);
  add(x: number, y: number) {
    const k = this.key(Math.floor(x / this.cell), Math.floor(y / this.cell));
    const l = this.cells.get(k);
    if (l) l.push(x, y);
    else this.cells.set(k, [x, y]);
  }
  get empty() { return this.cells.size === 0; }
  nearest(x: number, y: number, far: number, accept?: (px: number, py: number) => boolean): [number, number, number] | null {
    const ci = Math.floor(x / this.cell), cj = Math.floor(y / this.cell);
    let best = far * far, bx = 0, by = 0, found = false;
    for (let r = 0; r * this.cell <= far + this.cell; r++) {
      if (found && (r - 1) * this.cell > Math.sqrt(best)) break;
      for (let i = ci - r; i <= ci + r; i++)
        for (let j = cj - r; j <= cj + r; j++) {
          if (Math.max(Math.abs(i - ci), Math.abs(j - cj)) !== r) continue;
          const l = this.cells.get(this.key(i, j));
          if (!l) continue;
          for (let k = 0; k < l.length; k += 2) {
            const d = (l[k] - x) ** 2 + (l[k + 1] - y) ** 2;
            if (d < best && (!accept || accept(l[k], l[k + 1]))) (best = d), (bx = l[k]), (by = l[k + 1]), (found = true);
          }
        }
    }
    return found ? [Math.sqrt(best), bx, by] : null;
  }
}

const FAR = 80;

function samples(W: Float32Array, index: Uint32Array, t: number, step: number, at: (x: number, y: number, z: number, w: number) => void) {
  const a = 3 * index[3 * t], b = 3 * index[3 * t + 1], c = 3 * index[3 * t + 2];
  const ab = [W[b] - W[a], W[b + 1] - W[a + 1], W[b + 2] - W[a + 2]], ac = [W[c] - W[a], W[c + 1] - W[a + 1], W[c + 2] - W[a + 2]];
  const area = Math.hypot(ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]) / 2;
  const n = Math.max(1, Math.min(24, Math.ceil(Math.max(Math.hypot(...ab), Math.hypot(...ac)) / step)));
  const w = area / (n * n);
  const put = (u: number, v: number) => at(W[a] + u * ab[0] + v * ac[0], W[a + 1] + u * ab[1] + v * ac[1], W[a + 2] + u * ab[2] + v * ac[2], w);
  for (let i = 0; i < n; i++)
    for (let j = 0; i + j < n; j++) {
      put((i + 1 / 3) / n, (j + 1 / 3) / n);
      if (i + j < n - 1) put((i + 2 / 3) / n, (j + 2 / 3) / n);
    }
}

function oriented(P: ArrayLike<number>, index: Uint32Array): Uint32Array {
  const T = index.length / 3, out = Uint32Array.from(index), V = P.length / 3;
  const key = (a: number, b: number) => (a < b ? a * V + b : b * V + a);
  const edges = new Map<number, number[]>();
  for (let t = 0; t < T; t++)
    for (let e = 0; e < 3; e++) {
      const k = key(index[3 * t + e], index[3 * t + ((e + 1) % 3)]);
      const l = edges.get(k);
      if (l) l.push(t);
      else edges.set(k, [t]);
    }
  const hasDirected = (t: number, a: number, b: number) => [0, 1, 2].some((e) => out[3 * t + e] === a && out[3 * t + ((e + 1) % 3)] === b);
  const flip = (t: number) => { const x = out[3 * t + 1]; out[3 * t + 1] = out[3 * t + 2]; out[3 * t + 2] = x; };
  const seen = new Uint8Array(T);
  for (let s0 = 0; s0 < T; s0++) {
    if (seen[s0]) continue;
    const part: number[] = [s0];
    seen[s0] = 1;
    for (let q = 0; q < part.length; q++) {
      const t = part[q];
      for (let e = 0; e < 3; e++) {
        const a = out[3 * t + e], b = out[3 * t + ((e + 1) % 3)];
        const l = edges.get(key(a, b))!;
        if (l.length !== 2) continue;
        const n = l[0] === t ? l[1] : l[0];
        if (seen[n]) continue;
        seen[n] = 1;
        if (hasDirected(n, a, b)) flip(n);
        part.push(n);
      }
    }
    let vol = 0;
    for (const t of part) {
      const a = 3 * out[3 * t], b = 3 * out[3 * t + 1], c = 3 * out[3 * t + 2];
      vol += P[a] * (P[b + 1] * P[c + 2] - P[b + 2] * P[c + 1]) - P[a + 1] * (P[b] * P[c + 2] - P[b + 2] * P[c]) + P[a + 2] * (P[b] * P[c + 1] - P[b + 1] * P[c]);
    }
    if (vol < 0) part.forEach(flip);
  }
  return out;
}

export function analyze(inp: AnalyzeIn): Analysis {
  const t0 = performance.now();
  const { positions: P, matrix: m, firstLayer, nozzle } = inp;
  const index = oriented(P, inp.index);
  const V = P.length / 3, T = index.length / 3;
  const W = new Float32Array(P.length);
  let zMin = Infinity, zMax = -Infinity;
  for (let i = 0; i < V; i++) {
    const x = P[3 * i], y = P[3 * i + 1], z = P[3 * i + 2];
    W[3 * i] = m[0] * x + m[4] * y + m[8] * z + m[12];
    W[3 * i + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
    const wz = (W[3 * i + 2] = m[2] * x + m[6] * y + m[10] * z + m[14]);
    zMin = Math.min(zMin, wz), zMax = Math.max(zMax, wz);
  }
  for (let i = 0; i < V; i++) W[3 * i + 2] -= zMin;
  const height = zMax - zMin;
  const N = new Float32Array(T * 3), A = new Float32Array(T);
  for (let t = 0; t < T; t++) {
    const a = 3 * index[3 * t], b = 3 * index[3 * t + 1], c = 3 * index[3 * t + 2];
    const ux = W[b] - W[a], uy = W[b + 1] - W[a + 1], uz = W[b + 2] - W[a + 2];
    const vx = W[c] - W[a], vy = W[c + 1] - W[a + 1], vz = W[c + 2] - W[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, l = Math.hypot(nx, ny, nz);
    A[t] = l / 2;
    if (l > 0) (N[3 * t] = nx / l), (N[3 * t + 1] = ny / l), (N[3 * t + 2] = nz / l);
  }
  const zOf = (t: number, f: (a: number, b: number) => number) => f(f(W[3 * index[3 * t] + 2], W[3 * index[3 * t + 1] + 2]), W[3 * index[3 * t + 2] + 2]);

  const cut = firstLayer / 2 + 1e-5;
  const foot: number[] = [];
  let flat = 0;
  for (let t = 0; t < T; t++) {
    if (zOf(t, Math.min) < firstLayer) foot.push(t);
    if (zOf(t, Math.max) < 1e-3 && Math.abs(N[3 * t + 2]) > 0.99) flat += A[t];
  }
  const contact = Math.max(section(W, index, N, cut).area, flat);

  const limit = -Math.cos((inp.threshold * Math.PI) / 180);
  const inRegion = new Uint8Array(T);
  const parent = new Int32Array(V).fill(-1);
  const find = (v: number) => {
    while (parent[v] !== v) v = parent[v] = parent[parent[v]];
    return v;
  };
  for (let t = 0; t < T; t++) {
    if (!(N[3 * t + 2] < limit) || zOf(t, Math.max) < firstLayer) continue;
    inRegion[t] = 1;
    for (let k = 0; k < 3; k++) if (parent[index[3 * t + k]] < 0) parent[index[3 * t + k]] = index[3 * t + k];
    const r = find(index[3 * t]);
    parent[find(index[3 * t + 1])] = r;
    parent[find(index[3 * t + 2])] = r;
  }
  const groups = new Map<number, number[]>();
  for (let t = 0; t < T; t++) if (inRegion[t]) {
    const r = find(index[3 * t]);
    const g = groups.get(r);
    if (g) g.push(t);
    else groups.set(r, [t]);
  }
  const edgeKey = (a: number, b: number) => (a < b ? a * V + b : b * V + a);
  const regions: number[][] = [];
  for (const g of groups.values()) {
    let area = 0;
    for (const t of g) area += A[t];
    if (area >= 4) regions.push(g);
  }
  const bvh = buildBvh(W, index);
  const down = [0, 0, -1], probe = [0, 0, 0], hitDown = { tri: -1 };
  const solid = (x: number, y: number, z: number) => {
    (probe[0] = x), (probe[1] = y), (probe[2] = z);
    hitDown.tri = -1;
    return raycastBvh(bvh, W, index, probe, down, Infinity, hitDown) < Infinity && N[3 * hitDown.tri + 2] < 0;
  };
  const anchors = regions.map((g) => {
    const grid = new Grid(2);
    const open = new Map<number, number>();
    for (const t of g)
      for (let e = 0; e < 3; e++) {
        const k = edgeKey(index[3 * t + e], index[3 * t + ((e + 1) % 3)]);
        if (open.has(k)) open.delete(k);
        else open.set(k, 3 * t + e);
      }
    for (const at of open.values()) {
      const t = Math.floor(at / 3), e = at % 3;
      const a = 3 * index[at], b = 3 * index[3 * t + ((e + 1) % 3)], c = 3 * index[3 * t + ((e + 2) % 3)];
      const ex = W[b] - W[a], ey = W[b + 1] - W[a + 1], len = Math.hypot(ex, ey);
      let ox = -ey / (len || 1), oy = ex / (len || 1);
      if ((W[c] - W[a]) * ox + (W[c + 1] - W[a + 1]) * oy > 0) (ox = -ox), (oy = -oy);
      const n = Math.max(1, Math.ceil(len / 0.5));
      for (let i = 0; i <= n; i++) {
        const x = W[a] + (ex * i) / n, y = W[a + 1] + (ey * i) / n, z = W[a + 2] + ((W[b + 2] - W[a + 2]) * i) / n;
        if (z < firstLayer || (len > 0 && (solid(x + 0.3 * ox, y + 0.3 * oy, z - 0.5) || solid(x + 0.8 * ox, y + 0.8 * oy, z - 0.5)))) grid.add(x, y);
      }
    }
    return grid;
  });
  const overhangs: Region[] = [];
  for (let q = 0; q < regions.length; q++) {
    const g = regions[q];
    let area = 0, z = Infinity;
    for (const t of g) (area += A[t]), (z = Math.min(z, zOf(t, Math.min)));
    const pts: number[] = [];
    for (const t of g) samples(W, index, t, area > 4000 ? 2 : 1, (x, y, _z, w) => pts.push(x, y, w));
    const grid = anchors[q];
    const d = new Float32Array(pts.length / 3);
    for (let i = 0; i < d.length; i++) d[i] = grid.empty ? FAR : (grid.nearest(pts[3 * i], pts[3 * i + 1], FAR)?.[0] ?? FAR);
    const order = [...d.keys()].sort((i, j) => d[i] - d[j]);
    let acc = 0, worst = order[order.length - 1];
    for (const i of order) if ((acc += pts[3 * i + 2]) >= 0.95 * area) { worst = i; break; }
    const reach = d[worst];
    let bridge = false, span = reach;
    if (!grid.empty && reach < FAR) {
      const x = pts[3 * worst], y = pts[3 * worst + 1];
      const n = grid.nearest(x, y, FAR)!;
      const ux = (n[1] - x) / (n[0] || 1), uy = (n[2] - y) / (n[0] || 1);
      const far = grid.nearest(x, y, Math.min(FAR, 4 * reach + 2), (px, py) => (px - x) * ux + (py - y) * uy < -0.5 * Math.hypot(px - x, py - y));
      if (far) (bridge = true), (span = reach + far[0]);
    }
    overhangs.push({ tris: Uint32Array.from(g), area, reach, bridge, span, z });
  }

  let total = 0;
  for (let t = 0; t < T; t++) total += A[t];
  const step = Math.max(0.5, Math.sqrt(total / 40000));
  const thinMin = new Map<number, number>();
  const dir = [0, 0, 0], org = [0, 0, 0], hit = { tri: -1 };
  for (let t = 0; t < T; t++) {
    const nx = N[3 * t], ny = N[3 * t + 1], nz = N[3 * t + 2];
    if (!A[t] || Math.abs(nz) > 0.5) continue;
    if (A[t] < step * step && ((t * 2654435761) >>> 0) / 4294967296 > A[t] / (step * step)) continue;
    (dir[0] = -nx), (dir[1] = -ny), (dir[2] = -nz);
    const probe = (x: number, y: number, z: number) => {
      (org[0] = x - nx * 1e-3), (org[1] = y - ny * 1e-3), (org[2] = z - nz * 1e-3);
      const d = raycastBvh(bvh, W, index, org, dir, nozzle, hit);
      if (d >= nozzle || hit.tri === t) return;
      if (nx * N[3 * hit.tri] + ny * N[3 * hit.tri + 1] + nz * N[3 * hit.tri + 2] > -0.8) return;
      if (!(thinMin.get(t)! <= d)) thinMin.set(t, d);
    };
    if (A[t] < step * step) {
      const a = 3 * index[3 * t], b = 3 * index[3 * t + 1], c = 3 * index[3 * t + 2];
      probe((W[a] + W[b] + W[c]) / 3, (W[a + 1] + W[b + 1] + W[c + 1]) / 3, (W[a + 2] + W[b + 2] + W[c + 2]) / 3);
    } else samples(W, index, t, step * 1.5, probe);
  }
  const thinTris = Uint32Array.from(thinMin.keys());

  let slender = { ratio: 0, width: 0, z: 0 };
  if (height > 20)
    for (let k = 0; k < 24; k++) {
      const z = cut + ((height - cut) * k) / 24;
      if (height - z < 20) break;
      const s = section(W, index, N, z + 1e-4);
      if (s.width <= 0) continue;
      const ratio = (height - z) / s.width;
      if (ratio > slender.ratio) slender = { ratio, width: s.width, z };
    }
  const sway: number[] = [];
  if (slender.ratio > 0) for (let t = 0; t < T; t++) if (zOf(t, Math.min) >= slender.z) sway.push(t);

  return {
    height, contact, foot: Uint32Array.from(foot), overhangs,
    thin: { tris: thinTris, mm: Float32Array.from(thinTris, (t) => thinMin.get(t)!), area: Float32Array.from(thinTris, (t) => A[t]) },
    slender, sway: Uint32Array.from(sway), ms: performance.now() - t0,
  };
}
