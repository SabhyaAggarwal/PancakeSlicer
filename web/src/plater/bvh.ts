export interface Bvh {
  bounds: Float32Array;
  nodes: Uint32Array;
  tris: Uint32Array;
}

const LEAF = 0x80000000;
const MAX_LEAF = 16;

export function buildBvh(p: Float32Array, index: Uint32Array): Bvh {
  const n = index.length / 3;
  const cen = new Float32Array(n * 3);
  for (let t = 0; t < n; t++) {
    const a = 3 * index[3 * t], b = 3 * index[3 * t + 1], c = 3 * index[3 * t + 2];
    for (let k = 0; k < 3; k++) cen[3 * t + k] = (p[a + k] + p[b + k] + p[c + k]) / 3;
  }
  const tris = new Uint32Array(n);
  for (let t = 0; t < n; t++) tris[t] = t;
  const cap = Math.max(1, Math.ceil((2 * n) / (MAX_LEAF / 2)));
  let bounds = new Float32Array(cap * 6);
  let nodes = new Uint32Array(cap * 2);
  let count = 0;
  const grow = () => {
    const b = new Float32Array(bounds.length * 2), m = new Uint32Array(nodes.length * 2);
    b.set(bounds), m.set(nodes);
    bounds = b, nodes = m;
  };

  const node = (first: number, num: number, depth: number): number => {
    if (count * 2 >= nodes.length) grow();
    const id = count++;
    let lo0 = Infinity, lo1 = Infinity, lo2 = Infinity, hi0 = -Infinity, hi1 = -Infinity, hi2 = -Infinity;
    if (num > MAX_LEAF) {
      for (let i = first; i < first + num; i++) {
        const c = 3 * tris[i], x = cen[c], y = cen[c + 1], z = cen[c + 2];
        if (x < lo0) lo0 = x;
        if (x > hi0) hi0 = x;
        if (y < lo1) lo1 = y;
        if (y > hi1) hi1 = y;
        if (z < lo2) lo2 = z;
        if (z > hi2) hi2 = z;
      }
      const ext = [hi0 - lo0, hi1 - lo1, hi2 - lo2], mid = [(lo0 + hi0) / 2, (lo1 + hi1) / 2, (lo2 + hi2) / 2];
      const order = [0, 1, 2].sort((a, b) => ext[b] - ext[a]);
      let split = -1, axis = order[0];
      for (const ax of order) {
        if (!(ext[ax] > 0) || depth >= 40) break;
        let i = first, j = first + num - 1;
        while (i <= j) {
          if (cen[3 * tris[i] + ax] < mid[ax]) i++;
          else {
            const t = tris[i];
            tris[i] = tris[j], tris[j--] = t;
          }
        }
        if (i > first && i < first + num) {
          split = i, axis = ax;
          break;
        }
      }
      if (split < 0) split = first + (num >> 1);
      node(first, split - first, depth + 1);
      const right = node(split, first + num - split, depth + 1);
      nodes[2 * id] = right, nodes[2 * id + 1] = axis;
      for (let k = 0; k < 3; k++) {
        bounds[6 * id + k] = Math.min(bounds[6 * (id + 1) + k], bounds[6 * right + k]);
        bounds[6 * id + 3 + k] = Math.max(bounds[6 * (id + 1) + 3 + k], bounds[6 * right + 3 + k]);
      }
      return id;
    }
    for (let i = first; i < first + num; i++) {
      const t = 3 * tris[i];
      for (let v = 0; v < 3; v++) {
        const a = 3 * index[t + v], x = p[a], y = p[a + 1], z = p[a + 2];
        if (x < lo0) lo0 = x;
        if (x > hi0) hi0 = x;
        if (y < lo1) lo1 = y;
        if (y > hi1) hi1 = y;
        if (z < lo2) lo2 = z;
        if (z > hi2) hi2 = z;
      }
    }
    bounds[6 * id] = lo0, bounds[6 * id + 1] = lo1, bounds[6 * id + 2] = lo2;
    bounds[6 * id + 3] = hi0, bounds[6 * id + 4] = hi1, bounds[6 * id + 5] = hi2;
    nodes[2 * id] = first, nodes[2 * id + 1] = (num | LEAF) >>> 0;
    return id;
  };
  node(0, n, 0);
  return { bounds: bounds.slice(0, count * 6), nodes: nodes.slice(0, count * 2), tris };
}

const stack = new Uint32Array(128);
const stackT = new Float64Array(128);

export function supportBvh(bvh: Bvh, p: ArrayLike<number>, index: ArrayLike<number>, x: number, y: number, z: number): number {
  const { bounds, nodes, tris } = bvh;
  const upper = (i: number) => {
    const b = i * 6;
    return x * bounds[b + (x < 0 ? 0 : 3)] + y * bounds[b + (y < 0 ? 1 : 4)] + z * bounds[b + (z < 0 ? 2 : 5)];
  };
  let best = -Infinity, top = 0;
  stack[top++] = 0;
  while (top) {
    const i = stack[--top];
    if (upper(i) <= best) continue;
    const meta = nodes[i * 2 + 1];
    if (meta & LEAF) {
      const first = nodes[i * 2], end = first + (meta & ~LEAF);
      for (let k = first; k < end; ++k) for (let v = 0; v < 3; ++v) {
        const a = 3 * index[3 * tris[k] + v];
        best = Math.max(best, x * p[a] + y * p[a + 1] + z * p[a + 2]);
      }
    } else {
      const left = i + 1, right = nodes[i * 2];
      if (upper(left) > upper(right)) { stack[top++] = right; stack[top++] = left; }
      else { stack[top++] = left; stack[top++] = right; }
    }
  }
  return best;
}

export function raycastBvh(bvh: Bvh, p: ArrayLike<number>, index: ArrayLike<number>, o: ArrayLike<number>, d: ArrayLike<number>, far = Infinity, out?: { tri: number }): number {
  const { bounds, nodes, tris } = bvh;
  const ox = o[0], oy = o[1], oz = o[2], dx = d[0], dy = d[1], dz = d[2];
  const ix = 1 / dx, iy = 1 / dy, iz = 1 / dz;
  let best = far;
  const enter = (i: number) => {
    const b = 6 * i;
    let t0 = (bounds[b] - ox) * ix, t1 = (bounds[b + 3] - ox) * ix;
    let lo = Math.min(t0, t1), hi = Math.max(t0, t1);
    t0 = (bounds[b + 1] - oy) * iy, t1 = (bounds[b + 4] - oy) * iy;
    lo = Math.max(lo, Math.min(t0, t1)), hi = Math.min(hi, Math.max(t0, t1));
    t0 = (bounds[b + 2] - oz) * iz, t1 = (bounds[b + 5] - oz) * iz;
    lo = Math.max(lo, Math.min(t0, t1)), hi = Math.min(hi, Math.max(t0, t1));
    return hi < Math.max(lo, 0) || lo >= best ? Infinity : lo;
  };
  let top = 0;
  stackT[top] = enter(0), stack[top++] = 0;
  while (top) {
    const i = stack[--top];
    if (stackT[top] >= best) continue;
    const meta = nodes[2 * i + 1];
    if (meta & LEAF) {
      const first = nodes[2 * i], end = first + (meta & ~LEAF);
      for (let k = first; k < end; k++) {
        const t3 = 3 * tris[k];
        const a = 3 * index[t3], b = 3 * index[t3 + 1], c = 3 * index[t3 + 2];
        const e1x = p[b] - p[a], e1y = p[b + 1] - p[a + 1], e1z = p[b + 2] - p[a + 2];
        const e2x = p[c] - p[a], e2y = p[c + 1] - p[a + 1], e2z = p[c + 2] - p[a + 2];
        const px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
        const det = e1x * px + e1y * py + e1z * pz;
        if (det === 0) continue;
        const inv = 1 / det;
        const sx = ox - p[a], sy = oy - p[a + 1], sz = oz - p[a + 2];
        const u = (sx * px + sy * py + sz * pz) * inv;
        if (u < 0 || u > 1) continue;
        const qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
        const v = (dx * qx + dy * qy + dz * qz) * inv;
        if (v < 0 || u + v > 1) continue;
        const t = (e2x * qx + e2y * qy + e2z * qz) * inv;
        if (t >= 0 && t < best) {
          best = t;
          if (out) out.tri = tris[k];
        }
      }
      continue;
    }
    const l = i + 1, r = nodes[2 * i];
    const tl = enter(l), tr = enter(r);
    if (tl <= tr) {
      if (tr !== Infinity) stackT[top] = tr, stack[top++] = r;
      if (tl !== Infinity) stackT[top] = tl, stack[top++] = l;
    } else {
      if (tl !== Infinity) stackT[top] = tl, stack[top++] = l;
      stackT[top] = tr, stack[top++] = r;
    }
  }
  return best;
}
