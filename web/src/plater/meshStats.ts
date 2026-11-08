import type * as THREE from 'three';
import { request } from './geometry';

import type { MeshStats } from './meshStatsCore';
export type { MeshStats } from './meshStatsCore';

const pending = new WeakSet<THREE.BufferGeometry>();
const listeners = new Set<() => void>();
let version = 0;
export const meshStatsVersion = () => version;
export const subscribeMeshStats = (f: () => void) => (listeners.add(f), () => { listeners.delete(f); });

export function meshStats(g: THREE.BufferGeometry): MeshStats {
  const ready = g.userData.meshStats as MeshStats | undefined;
  if (ready) return ready;
  if (!pending.has(g)) {
    pending.add(g);
    request({ type: 'stats', positions: g.attributes.position.array as Float32Array, index: g.index?.array as Uint32Array | Uint16Array ?? null }, (m) => {
      if (m.type !== 'stats') return;
      g.userData.meshStats = m.stats;
      version++;
      listeners.forEach((f) => f());
    });
  }
  return { volume: 0, triangles: (g.index?.count ?? g.attributes.position.count) / 3, openEdges: 0 };
}
