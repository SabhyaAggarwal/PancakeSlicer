declare module 'three/examples/jsm/libs/meshopt_simplifier.module.js' {
  export const MeshoptSimplifier: {
    supported: boolean;
    ready: Promise<void>;
    simplify(indices: Uint32Array, positions: Float32Array, stride: number, target: number, error: number, flags?: string[]): [Uint32Array, number];
    simplifyWithAttributes(indices: Uint32Array, positions: Float32Array, stride: number, attributes: Float32Array, attributeStride: number, weights: number[], locks: Uint8Array | null, target: number, error: number, flags?: string[]): [Uint32Array, number];
    compactMesh(indices: Uint32Array): [Uint32Array, number];
  };
}
