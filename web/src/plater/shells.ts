export const PIP_GAP_MM = 0.6;
const CONTACT_MM = 0.08;
const CELL = PIP_GAP_MM;
const MIN_GAP_AREA_MM2 = 60;
const MAX_CONTACT = 0.05;
const SAMPLE_MM = 1.2;
const MIN_VOLUME_MM3 = 1;
const MAX_ENTRIES = 4_000_000;

function shellsOf(index: Uint32Array, vertices: number): { shell: Int32Array; count: number } {
  const parent = new Int32Array(vertices);
  for (let i = 0; i < vertices; i++) parent[i] = i;
  const find = (v: number) => {
    while (parent[v] !== v) v = parent[v] = parent[parent[v]];
    return v;
  };
  for (let t = 0; t < index.length; t += 3) {
    const a = find(index[t]), b = find(index[t + 1]), c = find(index[t + 2]);
    parent[b] = a;
    parent[c] = a;
  }
  const ids = new Int32Array(vertices).fill(-1);
  const shell = new Int32Array(index.length / 3);
  let count = 0;
  for (let t = 0; t < shell.length; t++) {
    const r = find(index[t * 3]);
    if (ids[r] < 0) ids[r] = count++;
    shell[t] = ids[r];
  }
  return { shell, count };
}

function pointTriangle(p: number[], P: Float32Array, a: number, b: number, c: number): number {
  const ax = P[a], ay = P[a + 1], az = P[a + 2];
  const abx = P[b] - ax, aby = P[b + 1] - ay, abz = P[b + 2] - az;
  const acx = P[c] - ax, acy = P[c + 1] - ay, acz = P[c + 2] - az;
  const apx = p[0] - ax, apy = p[1] - ay, apz = p[2] - az;
  const d1 = abx * apx + aby * apy + abz * apz, d2 = acx * apx + acy * apy + acz * apz;
  const bpx = p[0] - P[b], bpy = p[1] - P[b + 1], bpz = p[2] - P[b + 2];
  const d3 = abx * bpx + aby * bpy + abz * bpz, d4 = acx * bpx + acy * bpy + acz * bpz;
  const cpx = p[0] - P[c], cpy = p[1] - P[c + 1], cpz = p[2] - P[c + 2];
  const d5 = abx * cpx + aby * cpy + abz * cpz, d6 = acx * cpx + acy * cpy + acz * cpz;
  let u: number, v: number;
  if (d1 <= 0 && d2 <= 0) (u = 0), (v = 0);
  else if (d3 >= 0 && d4 <= d3) (u = 1), (v = 0);
  else if (d6 >= 0 && d5 <= d6) (u = 0), (v = 1);
  else {
    const vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) (u = d1 / (d1 - d3)), (v = 0);
    else if (vb <= 0 && d2 >= 0 && d6 <= 0) (u = 0), (v = d2 / (d2 - d6));
    else if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
      const w = (d4 - d3) / (d4 - d3 + (d5 - d6));
      (u = 1 - w), (v = w);
    } else {
      const s = 1 / (va + vb + vc);
      (u = vb * s), (v = vc * s);
    }
  }
  const dx = apx - u * abx - v * acx, dy = apy - u * aby - v * acy, dz = apz - u * abz - v * acz;
  return dx * dx + dy * dy + dz * dz;
}

function inside(P: Float32Array, index: Uint32Array, tris: number[], x: number, y: number, z: number): boolean {
  x += 1.37e-4;
  y += 2.11e-4;
  let n = 0;
  for (const t of tris) {
    const a = index[t * 3] * 3, b = index[t * 3 + 1] * 3, c = index[t * 3 + 2] * 3;
    const d = (P[b + 1] - P[c + 1]) * (P[a] - P[c]) + (P[c] - P[b]) * (P[a + 1] - P[c + 1]);
    if (d === 0) continue;
    const u = ((P[b + 1] - P[c + 1]) * (x - P[c]) + (P[c] - P[b]) * (y - P[c + 1])) / d;
    const v = ((P[c + 1] - P[a + 1]) * (x - P[c]) + (P[a] - P[c]) * (y - P[c + 1])) / d;
    if (u < 0 || v < 0 || u + v > 1) continue;
    if (u * P[a + 2] + v * P[b + 2] + (1 - u - v) * P[c + 2] > z) n++;
  }
  return (n & 1) === 1;
}

export function printInPlaceGap(P: Float32Array, index: Uint32Array): number | null {
  const { shell, count } = shellsOf(index, P.length / 3);
  if (count < 2 || count > 500) return null;
  const lo = new Float64Array(count * 3).fill(Infinity), hi = new Float64Array(count * 3).fill(-Infinity);
  const volume = new Float64Array(count);
  for (let t = 0; t < shell.length; t++) {
    const s = shell[t];
    const a = index[t * 3] * 3, b = index[t * 3 + 1] * 3, c = index[t * 3 + 2] * 3;
    volume[s] += (P[a] * (P[b + 1] * P[c + 2] - P[b + 2] * P[c + 1]) - P[a + 1] * (P[b] * P[c + 2] - P[b + 2] * P[c]) + P[a + 2] * (P[b] * P[c + 1] - P[b + 1] * P[c])) / 6;
    for (let k = 0; k < 3; k++) {
      const v = index[t * 3 + k] * 3;
      for (let a = 0; a < 3; a++) {
        lo[s * 3 + a] = Math.min(lo[s * 3 + a], P[v + a]);
        hi[s * 3 + a] = Math.max(hi[s * 3 + a], P[v + a]);
      }
    }
  }
  const pairs: [number, number][] = [];
  for (let i = 0; i < count; i++) {
    if (volume[i] < MIN_VOLUME_MM3) continue;
    for (let j = i + 1; j < count; j++) {
      if (volume[j] < MIN_VOLUME_MM3) continue;
      let overlap = true;
      for (let a = 0; a < 3 && overlap; a++) overlap = lo[i * 3 + a] < hi[j * 3 + a] && lo[j * 3 + a] < hi[i * 3 + a];
      if (overlap) pairs.push([i, j]);
    }
  }
  if (!pairs.length) return null;
  const paired = new Set(pairs.map(([i, j]) => i * count + j));

  const region = (i: number, j: number, a: number): [number, number] =>
    [Math.max(lo[i * 3 + a], lo[j * 3 + a]) - PIP_GAP_MM, Math.min(hi[i * 3 + a], hi[j * 3 + a]) + PIP_GAP_MM];
  const regions = pairs.map(([i, j]) => [0, 1, 2].map((a) => region(i, j, a)));
  const inRegion = (r: [number, number][], x: number, y: number, z: number) =>
    x >= r[0][0] && x <= r[0][1] && y >= r[1][0] && y <= r[1][1] && z >= r[2][0] && z <= r[2][1];
  const grid = new Map<number, number[]>();
  const key = (x: number, y: number, z: number) => ((x * 73856093) ^ (y * 19349663) ^ (z * 83492791)) | 0;
  const cell = (v: number) => Math.floor(v / CELL);
  let entries = 0;
  for (let t = 0; t < shell.length; t++) {
    const s = shell[t];
    const a = index[t * 3] * 3, b = index[t * 3 + 1] * 3, c = index[t * 3 + 2] * 3;
    const t0 = [0, 1, 2].map((k) => Math.min(P[a + k], P[b + k], P[c + k]));
    const t1 = [0, 1, 2].map((k) => Math.max(P[a + k], P[b + k], P[c + k]));
    for (let p = 0; p < pairs.length; p++) {
      if (pairs[p][0] !== s && pairs[p][1] !== s) continue;
      const r = regions[p];
      const x0 = Math.max(t0[0], r[0][0]), x1 = Math.min(t1[0], r[0][1]);
      const y0 = Math.max(t0[1], r[1][0]), y1 = Math.min(t1[1], r[1][1]);
      const z0 = Math.max(t0[2], r[2][0]), z1 = Math.min(t1[2], r[2][1]);
      if (x0 > x1 || y0 > y1 || z0 > z1) continue;
      for (let x = cell(x0); x <= cell(x1); x++)
        for (let y = cell(y0); y <= cell(y1); y++)
          for (let z = cell(z0); z <= cell(z1); z++) {
            const k = key(x, y, z);
            const list = grid.get(k);
            if (list) {
              if (list[list.length - 1] !== t) list.push(t);
            } else grid.set(k, [t]);
            if (++entries > MAX_ENTRIES) return null;
          }
    }
  }

  const gapArea = new Map<number, number>();
  const contactArea = new Map<number, number>();
  const nearest = new Map<number, number>();
  const r2 = PIP_GAP_MM * PIP_GAP_MM, c2 = CONTACT_MM * CONTACT_MM;
  const p = [0, 0, 0];
  const near = new Map<number, number>();
  const probe = (s: number, x: number, y: number, z: number, w: number) => {
    if (!regions.some((r, q) => (pairs[q][0] === s || pairs[q][1] === s) && inRegion(r, x, y, z))) return;
    (p[0] = x), (p[1] = y), (p[2] = z);
    near.clear();
    const cx = cell(x), cy = cell(y), cz = cell(z);
    for (let dx = -1; dx <= 1; dx++)
      for (let dy = -1; dy <= 1; dy++)
        for (let dz = -1; dz <= 1; dz++) {
          const list = grid.get(key(cx + dx, cy + dy, cz + dz));
          if (!list) continue;
          for (const t of list) {
            const o = shell[t];
            if (o === s) continue;
            const d2 = pointTriangle(p, P, index[t * 3] * 3, index[t * 3 + 1] * 3, index[t * 3 + 2] * 3);
            if (d2 <= r2 && !(near.get(o)! <= d2)) near.set(o, d2);
          }
        }
    for (const [o, d2] of near) {
      const pk = Math.min(s, o) * count + Math.max(s, o);
      if (!paired.has(pk)) continue;
      gapArea.set(pk, (gapArea.get(pk) ?? 0) + w);
      if (d2 < c2) contactArea.set(pk, (contactArea.get(pk) ?? 0) + w);
      else if (!(nearest.get(pk)! <= d2)) nearest.set(pk, d2);
    }
  };
  for (let t = 0; t < shell.length; t++) {
    const s = shell[t];
    const a = index[t * 3] * 3, b = index[t * 3 + 1] * 3, c = index[t * 3 + 2] * 3;
    const ab = [P[b] - P[a], P[b + 1] - P[a + 1], P[b + 2] - P[a + 2]], ac = [P[c] - P[a], P[c + 1] - P[a + 1], P[c + 2] - P[a + 2]];
    const area = Math.hypot(ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]) / 2;
    const n = Math.max(1, Math.min(32, Math.ceil(Math.max(Math.hypot(...ab), Math.hypot(...ac)) / SAMPLE_MM)));
    const w = area / (n * n);
    const at = (u: number, v: number) => probe(s, P[a] + u * ab[0] + v * ac[0], P[a + 1] + u * ab[1] + v * ac[1], P[a + 2] + u * ab[2] + v * ac[2], w);
    for (let i = 0; i < n; i++)
      for (let j = 0; i + j < n; j++) {
        at((i + 1 / 3) / n, (j + 1 / 3) / n);
        if (i + j < n - 1) at((i + 2 / 3) / n, (j + 2 / 3) / n);
      }
  }
  const members: number[][] = Array.from({ length: count }, () => []);
  for (let t = 0; t < shell.length; t++) members[shell[t]].push(t);
  const fused = (i: number, j: number) => {
    const q = pairs.findIndex(([a, b]) => a === i && b === j);
    for (const [s, o] of [[i, j], [j, i]]) {
      const tris = members[s];
      const step = Math.max(1, Math.floor(tris.length / 16));
      for (let k = 0; k < tris.length; k += step) {
        const v = index[tris[k] * 3] * 3;
        if (inRegion(regions[q], P[v], P[v + 1], P[v + 2]) && inside(P, index, members[o], P[v], P[v + 1], P[v + 2])) return true;
      }
    }
    return false;
  };
  let gap = Infinity;
  for (const [pk, all] of gapArea) {
    const touching = contactArea.get(pk) ?? 0;
    if ((all - touching) / 2 >= MIN_GAP_AREA_MM2 && touching <= MAX_CONTACT * all && nearest.get(pk)! < gap && !fused(Math.floor(pk / count), pk % count))
      gap = nearest.get(pk)!;
  }
  return Number.isFinite(gap) ? Math.sqrt(gap) : null;
}
