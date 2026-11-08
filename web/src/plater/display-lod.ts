export interface DisplayMesh {
  positions: Float32Array;
  normals: Float32Array;
  index: Uint32Array;
  edges?: Float32Array;
}

export function displayLod(positions: Float32Array, index: Uint32Array, normals: Float32Array, min: number[], max: number[]): DisplayMesh | undefined {
  if (index.length < 300_000 * 3) return;
  const diagonal = Math.hypot(max[0] - min[0], max[1] - min[1], max[2] - min[2]);
  const cell = Math.max(0.05, diagonal / 400);
  const n = positions.length / 3;
  let size = 1;
  while (size < n * 1.5) size *= 2;
  const table = new Int32Array(size).fill(-1), mask = size - 1;
  const mapped = new Uint32Array(n), keys = new Int32Array(n * 3);
  const p = new Float32Array(positions.length), normal = new Float32Array(normals.length), counts = new Uint32Array(n);
  let count = 0;
  for (let i = 0; i < n; ++i) {
    const x = Math.floor((positions[i * 3] - min[0]) / cell), y = Math.floor((positions[i * 3 + 1] - min[1]) / cell), z = Math.floor((positions[i * 3 + 2] - min[2]) / cell);
    let hash = (Math.imul(x, 73856093) ^ Math.imul(y, 19349663) ^ Math.imul(z, 83492791)) & mask;
    while (table[hash] >= 0) {
      const j = table[hash];
      if (keys[j * 3] === x && keys[j * 3 + 1] === y && keys[j * 3 + 2] === z) break;
      hash = (hash + 1) & mask;
    }
    if (table[hash] < 0) {
      table[hash] = count;
      keys[count * 3] = x; keys[count * 3 + 1] = y; keys[count * 3 + 2] = z;
      count++;
    }
    const j = table[hash]; mapped[i] = j; counts[j]++;
    for (let k = 0; k < 3; ++k) { p[j * 3 + k] += positions[i * 3 + k]; normal[j * 3 + k] += normals[i * 3 + k]; }
  }
  for (let i = 0; i < count; ++i) {
    const length = Math.hypot(normal[i * 3], normal[i * 3 + 1], normal[i * 3 + 2]) || 1;
    for (let k = 0; k < 3; ++k) { p[i * 3 + k] /= counts[i]; normal[i * 3 + k] /= length; }
  }
  const triangles = new Uint32Array(index.length);
  let length = 0;
  for (let i = 0; i < index.length; i += 3) {
    const a = mapped[index[i]], b = mapped[index[i + 1]], c = mapped[index[i + 2]];
    if (a === b || b === c || c === a) continue;
    triangles[length++] = a; triangles[length++] = b; triangles[length++] = c;
  }
  if (length >= index.length * 0.7) return;
  return { positions: p.slice(0, count * 3), normals: normal.slice(0, count * 3), index: triangles.slice(0, length) };
}
