export interface MeshStats { volume: number; triangles: number; openEdges: number }

export function computeMeshStats(p: Float32Array, index: Uint32Array | Uint16Array | null): MeshStats {
  const triangles = index ? index.length / 3 : p.length / 9;
  let size = 1;
  if (index) while (size < triangles * 2) size *= 2;
  let low = new Uint32Array(size), high = new Uint32Array(size), counts = new Uint8Array(size);
  let used = 0, openEdges = index ? 0 : triangles * 3, volume = 0;
  const hash = (a: number, b: number) => {
    const h = Math.imul(a, 0x9e3779b1) ^ Math.imul(b, 0x85ebca6b);
    return (h ^ (h >>> 16)) >>> 0;
  };
  const edge = (a: number, b: number) => {
    const x = Math.min(a, b), y = Math.max(a, b);
    let h = hash(x, y) & (size - 1);
    while (counts[h] && (low[h] !== x || high[h] !== y)) h = (h + 1) & (size - 1);
    if (counts[h]) {
      if (counts[h] === 1) { counts[h] = 2; openEdges--; }
      return;
    }
    low[h] = x; high[h] = y; counts[h] = 1; used++; openEdges++;
    if (used < size * 0.75) return;
    const oldLow = low, oldHigh = high, oldCounts = counts;
    size *= 2;
    low = new Uint32Array(size); high = new Uint32Array(size); counts = new Uint8Array(size);
    for (let i = 0; i < oldCounts.length; ++i) if (oldCounts[i]) {
      let k = hash(oldLow[i], oldHigh[i]) & (size - 1);
      while (counts[k]) k = (k + 1) & (size - 1);
      low[k] = oldLow[i]; high[k] = oldHigh[i]; counts[k] = oldCounts[i];
    }
  };
  for (let t = 0; t < triangles * 3; t += 3) {
    const a = index ? index[t] : t, b = index ? index[t + 1] : t + 1, c = index ? index[t + 2] : t + 2;
    volume += (p[3 * a] * (p[3 * b + 1] * p[3 * c + 2] - p[3 * b + 2] * p[3 * c + 1]) - p[3 * a + 1] * (p[3 * b] * p[3 * c + 2] - p[3 * b + 2] * p[3 * c])
      + p[3 * a + 2] * (p[3 * b] * p[3 * c + 1] - p[3 * b + 1] * p[3 * c])) / 6;
    if (index) { edge(a, b); edge(b, c); edge(c, a); }
  }
  return { volume: Math.abs(volume), triangles, openEdges };
}
