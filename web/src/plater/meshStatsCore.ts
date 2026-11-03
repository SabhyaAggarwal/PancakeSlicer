export interface MeshStats { volume: number; triangles: number; openEdges: number }

export function computeMeshStats(p: Float32Array, index: Uint32Array | Uint16Array | null): MeshStats {
  const triangles = index ? index.length / 3 : p.length / 9, vertices = p.length / 3;
  const edges = new Map<number, number>();
  let volume = 0;
  const edge = (a: number, b: number) => {
    const key = Math.min(a, b) * vertices + Math.max(a, b);
    edges.set(key, (edges.get(key) ?? 0) + 1);
  };
  for (let t = 0; t < triangles * 3; t += 3) {
    const a = index ? index[t] : t, b = index ? index[t + 1] : t + 1, c = index ? index[t + 2] : t + 2;
    volume += (p[3 * a] * (p[3 * b + 1] * p[3 * c + 2] - p[3 * b + 2] * p[3 * c + 1]) - p[3 * a + 1] * (p[3 * b] * p[3 * c + 2] - p[3 * b + 2] * p[3 * c])
      + p[3 * a + 2] * (p[3 * b] * p[3 * c + 1] - p[3 * b + 1] * p[3 * c])) / 6;
    edge(a, b); edge(b, c); edge(c, a);
  }
  let openEdges = 0;
  for (const count of edges.values()) if (count === 1) openEdges++;
  return { volume: Math.abs(volume), triangles, openEdges };
}
