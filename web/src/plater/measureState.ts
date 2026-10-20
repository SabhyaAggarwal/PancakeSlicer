import type * as THREE from 'three';

type V = THREE.Vector3;
export type Edge = [V, V];

export type Feature =
  | { type: 'point'; p: V; label: string }
  | { type: 'edge'; a: V; b: V }
  | { type: 'circle'; c: V; r: number; normal: V }
  | { type: 'plane'; normal: V; origin: V; tris: Float32Array; edges: Edge[]; id: string };

export const near = (a: V, b: V) => a.distanceToSquared(b) < 1e-10;
export function sameFeature(a: Feature | undefined, b: Feature | undefined): boolean {
  if (!a || !b || a.type !== b.type) return false;
  if (a.type === 'point') return near(a.p, (b as typeof a).p);
  if (a.type === 'edge') {
    const e = b as typeof a;
    return (near(a.a, e.a) && near(a.b, e.b)) || (near(a.a, e.b) && near(a.b, e.a));
  }
  if (a.type === 'circle') return near(a.c, (b as typeof a).c) && Math.abs(a.r - (b as typeof a).r) < 1e-6;
  return a.id === (b as typeof a).id;
}

export function centreOf(f: Feature | undefined): Feature | undefined {
  if (f?.type === 'circle') return { type: 'point', p: f.c.clone(), label: 'Center of circle' };
  if (f?.type === 'edge') return { type: 'point', p: f.a.clone().add(f.b).multiplyScalar(0.5), label: 'Center of edge' };
  return undefined;
}

export interface MeasureState { hover?: Feature; first?: Feature; second?: Feature }
let state: MeasureState = {};
const listeners = new Set<() => void>();
export const measureState = () => state;
export function setMeasure(next: MeasureState) {
  state = next;
  listeners.forEach((l) => l());
}
export function subscribeMeasure(l: () => void) {
  listeners.add(l);
  return () => listeners.delete(l);
}

export function selectFeature(f: Feature) {
  const { first, second } = state;
  const swaps = (sel: Feature | undefined) => !!sel && (sameFeature(f, centreOf(sel)) || sameFeature(centreOf(f), sel));
  if (!sameFeature(f, first) && swaps(first)) setMeasure({ ...state, first: f });
  else if (!sameFeature(f, second) && swaps(second)) setMeasure({ ...state, second: f });
  else if (sameFeature(f, first)) setMeasure({ ...state, first: second, second: undefined });
  else if (sameFeature(f, second)) setMeasure({ ...state, second: undefined });
  else if (!first) setMeasure({ ...state, first: f });
  else setMeasure({ ...state, second: f });
}

export function dropLastFeature(): boolean {
  if (state.second) setMeasure({ ...state, second: undefined });
  else if (state.first) setMeasure({ ...state, first: undefined });
  else return false;
  return true;
}
