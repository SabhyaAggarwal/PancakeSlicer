import { useEffect, useReducer } from 'react';
import type { JobStatus } from '../api/types';
import { RANGE_PALETTE, type RGB } from './colors';

export interface SurfaceGrid {
  pitch: number;
  origin: [number, number];
  nx: number;
  ny: number;
  rms: Float32Array;
  meshZ: Float32Array;
  eligible: Float32Array;
}

export const ERROR_BINS: { label: string; upTo: number; color: RGB }[] = [
  { label: '0–10', upTo: 10, color: RANGE_PALETTE[0] },
  { label: '10–20', upTo: 20, color: RANGE_PALETTE[3] },
  { label: '20–40', upTo: 40, color: RANGE_PALETTE[5] },
  { label: '40–70', upTo: 70, color: RANGE_PALETTE[7] },
  { label: '70+', upTo: Infinity, color: RANGE_PALETTE[10] },
];
export const binOf = (um: number) => ERROR_BINS.find((b) => um < b.upTo)!;

function f32(b64: unknown, n: number): Float32Array | null {
  if (typeof b64 !== 'string') return null;
  const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  if (bytes.length !== n * 4) return null;
  const dv = new DataView(bytes.buffer);
  const out = new Float32Array(n);
  for (let i = 0; i < n; ++i) out[i] = dv.getFloat32(i * 4, true);
  return out;
}

function decode(g: Record<string, unknown>): SurfaceGrid | null {
  const nx = Number(g.nx), ny = Number(g.ny), n = nx * ny;
  const origin = g.origin as number[];
  const rms = f32(g.rms_mm, n), meshZ = f32(g.mesh_z_mm, n), eligible = f32(g.eligible_fraction, n);
  return Number.isInteger(n) && n > 0 && rms && meshZ && eligible && Array.isArray(origin)
    ? { pitch: Number(g.pitch_mm), origin: [Number(origin[0]), Number(origin[1])] as [number, number], nx, ny, rms, meshZ, eligible }
    : null;
}

const loaded = new Map<string, SurfaceGrid | null>();
const pending = new Map<string, Promise<void>>();

export function loadSurfaceGrid(id: string): Promise<void> {
  if (loaded.has(id)) return Promise.resolve();
  let p = pending.get(id);
  if (!p) {
    p = fetch(`/api/jobs/${id}/surface-grid`)
      .then(async (r) => {
        if (!r.ok) throw new Error(`${r.status}`);
        const g = await r.json();
        loaded.set(id, g && typeof g === 'object' ? decode(g) : null);
      })
      .finally(() => pending.delete(id));
    pending.set(id, p);
  }
  return p;
}

export function useSurfaceGrid(status: JobStatus | undefined, wanted: boolean): SurfaceGrid | null {
  const id = status?.has_surface_grid ? status.id : null;
  const [, bump] = useReducer((x: number) => x + 1, 0);
  useEffect(() => {
    if (!id || !wanted || loaded.has(id)) return;
    let live = true;
    loadSurfaceGrid(id).then(() => live && bump(), () => {});
    return () => { live = false; };
  }, [id, wanted]);
  return id && wanted ? loaded.get(id) ?? null : null;
}
