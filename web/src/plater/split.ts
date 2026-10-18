import { bakeStl } from './meshcore';

export function split(soup: Float32Array): Blob[] {
  const vertices = new Map<string, number>();
  const index = new Uint32Array(soup.length / 3);
  for (let v = 0; v < index.length; v++) {
    const key = `${soup[v * 3]},${soup[v * 3 + 1]},${soup[v * 3 + 2]}`;
    let id = vertices.get(key);
    if (id === undefined) vertices.set(key, id = vertices.size);
    index[v] = id;
  }
  const neighbors = new Int32Array(index.length).fill(-1);
  const edges = new Map<string, number[]>();
  for (let e = 0; e < index.length; e++) {
    const next = e - e % 3 + (e + 1) % 3;
    const key = `${index[e]},${index[next]}`;
    const list = edges.get(key) ?? [];
    list.push(e);
    edges.set(key, list);
  }
  for (let e = 0; e < index.length; e++) {
    if (neighbors[e] !== -1) continue;
    const next = e - e % 3 + (e + 1) % 3;
    const reverse = edges.get(`${index[next]},${index[e]}`);
    const other = reverse?.find((r) => Math.floor(r / 3) > Math.floor(e / 3) && neighbors[r] === -1);
    if (other !== undefined) {
      neighbors[e] = Math.floor(other / 3);
      neighbors[other] = Math.floor(e / 3);
    }
  }
  const visited = new Uint8Array(index.length / 3);
  const pieces: Blob[] = [];
  const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
  for (let seed = 0; seed < visited.length; seed++) {
    if (visited[seed]) continue;
    const faces = [seed], stack = [seed];
    visited[seed] = 1;
    while (stack.length) {
      const t = stack.pop()!;
      for (let k = 0; k < 3; k++) {
        const n = neighbors[t * 3 + k];
        if (n >= 0 && !visited[n]) {
          visited[n] = 1;
          faces.push(n);
          stack.push(n);
        }
      }
    }
    if (faces.length < 3) continue;
    const part = new Float32Array(faces.length * 9);
    faces.forEach((t, i) => part.set(soup.subarray(t * 9, t * 9 + 9), i * 9));
    let volume = 0;
    const ox = part[0], oy = part[1], oz = part[2];
    for (let t = 0; t < part.length; t += 9) {
      const ax = part[t] - ox, ay = part[t + 1] - oy, az = part[t + 2] - oz;
      const bx = part[t + 3] - ox, by = part[t + 4] - oy, bz = part[t + 5] - oz;
      const cx = part[t + 6] - ox, cy = part[t + 7] - oy, cz = part[t + 8] - oz;
      volume += ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
    }
    if (volume < 0) for (let t = 0; t < part.length; t += 9)
      for (let k = 0; k < 3; k++) [part[t + 3 + k], part[t + 6 + k]] = [part[t + 6 + k], part[t + 3 + k]];
    pieces.push(bakeStl(part, null, identity));
  }
  return pieces;
}
