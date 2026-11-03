import { MeshoptSimplifier } from 'three/examples/jsm/libs/meshopt_simplifier.module.js';

export interface SimplificationInfo {
  trianglesBefore: number;
  trianglesAfter: number;
  maxErrorMm: number;
  kept: 'simplified' | 'original';
}

export async function simplifyMesh(positions: Float32Array, index: Uint32Array, options: { target?: number; maxErrorMm?: number; minTriangles?: number } = {}): Promise<{ positions: Float32Array; index: Uint32Array; info: SimplificationInfo } | null> {
  if (index.length < (options.minTriangles ?? 500_000) * 3 || !MeshoptSimplifier.supported) return null;
  await MeshoptSimplifier.ready;
  const extrema = [0, 0, 0, 0, 0, 0];
  for (let i = 1; i < positions.length / 3; ++i) for (let k = 0; k < 3; ++k) {
    if (positions[i * 3 + k] < positions[extrema[k] * 3 + k]) extrema[k] = i;
    if (positions[i * 3 + k] > positions[extrema[k + 3] * 3 + k]) extrema[k + 3] = i;
  }
  const locks = new Uint8Array(positions.length / 3);
  for (const i of extrema) locks[i] = 1;
  const [reduced, error] = MeshoptSimplifier.simplifyWithAttributes(index, positions, 3, new Float32Array(0), 0, [], locks,
    Math.min(index.length, (options.target ?? 150_000) * 3), options.maxErrorMm ?? 0.02, ['LockBorder', 'ErrorAbsolute']);
  if (!reduced.length || reduced.length >= index.length * 0.8) return null;
  const [remap, vertices] = MeshoptSimplifier.compactMesh(reduced);
  const compact = new Float32Array(vertices * 3);
  for (let i = 0; i < remap.length; ++i) {
    const to = remap[i];
    if (to === 0xffffffff) continue;
    compact[to * 3] = positions[i * 3]; compact[to * 3 + 1] = positions[i * 3 + 1]; compact[to * 3 + 2] = positions[i * 3 + 2];
  }
  return { positions: compact, index: reduced, info: { trianglesBefore: index.length / 3, trianglesAfter: reduced.length / 3, maxErrorMm: error, kept: 'simplified' } };
}
