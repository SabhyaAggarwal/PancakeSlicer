import { useSyncExternalStore } from 'react';
import { jobRequest } from './actions';
import { canForcePlanar } from './api/client';
import type { JobStatus } from './api/types';
import type { PreviewData } from './preview/data';
import { jobValue } from './options';
import { getState } from './state/store';

export type CompareState =
  | { state: 'idle' }
  | { state: 'running'; key: string; queued: boolean; pct: number }
  | { state: 'done'; key: string; id: string; status: JobStatus }
  | { state: 'failed'; key: string; error: string; busy: boolean };

let current: CompareState = { state: 'idle' };
const listeners = new Set<() => void>();
const cache = new Map<string, { id: string; status: JobStatus }>();
let active: { id?: string; stop?: () => void; submission: AbortController } | null = null;
let generation = 0;

function set(next: CompareState) {
  current = next;
  for (const l of listeners) l();
}

const subscribe = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export function useCompare(): CompareState {
  return useSyncExternalStore(subscribe, () => current);
}

let wanted = false;
export function setCompareWanted(on: boolean) {
  wanted = on;
  for (const l of listeners) l();
}
export function useCompareWanted(): boolean {
  return useSyncExternalStore(subscribe, () => wanted);
}

let standardData: PreviewData | null = null;
export function setStandardData(d: PreviewData | null) {
  standardData = d;
  for (const l of listeners) l();
}
export function useStandardData(): PreviewData | null {
  return useSyncExternalStore(subscribe, () => standardData);
}

async function standardRequest() {
  const req = await jobRequest(getState());
  const flat = { nonplanar_mode: jobValue('nonplanar_mode', 'off'), zaa_enabled: false };
  return {
    ...req,
    overrides: { ...req.overrides, ...flat },
    plate: req.plate && { objects: req.plate.objects.map((o) => {
      const { nonplanar_mode: _, ...rest } = o.settings ?? {};
      return { ...o, settings: canForcePlanar() ? { ...rest, zaa_enabled: '0' } : { ...rest, nonplanar_mode: 'off', zaa_enabled: '0' } };
    }) },
    project_settings: req.plate ? undefined : canForcePlanar() ? { zaa_enabled: false } : flat,
    force_planar: canForcePlanar() || undefined,
    output: 'gcode' as const,
    priority: 'low' as const,
  };
}

export async function startCompare() {
  const s = getState();
  const key = s.job.plateSignature;
  if (!s.api || s.job.state !== 'done' || !key) return;
  const kept = cache.get(key);
  if (kept) return set({ state: 'done', key, ...kept });
  if (current.state === 'running' && current.key === key) return;
  stopCompare();
  const mine = ++generation;
  const submission = new AbortController();
  active = { submission };
  set({ state: 'running', key, queued: true, pct: 0 });
  try {
    const id = await s.api.submit(await standardRequest(), submission.signal);
    if (mine !== generation) return void s.api.cancel(id).catch(() => undefined);
    active.id = id;
    active.stop = s.api.events(id, {
      progress: (e) => mine === generation && set({ state: 'running', key, queued: e.stage === 'queued', pct: Math.max(0, Math.min(100, e.pct)) }),
      done: (status) => {
        if (mine !== generation) return;
        cache.set(key, { id, status });
        active = null;
        set({ state: 'done', key, id, status });
      },
      error: (error) => {
        if (mine !== generation) return;
        active = null;
        set({ state: 'failed', key, error, busy: false });
      },
    });
  } catch (e) {
    if (mine !== generation) return;
    active = null;
    const status = (e as { status?: number }).status;
    set({ state: 'failed', key, error: String((e as Error).message ?? e), busy: status === 429 || status === 503 });
  }
}

export function stopCompare() {
  ++generation;
  const a = active;
  active = null;
  a?.submission.abort();
  a?.stop?.();
  if (a?.id) void getState().api?.cancel(a.id).catch(() => undefined);
  if (current.state !== 'idle') set({ state: 'idle' });
}

export function forgetCompare(key: string) {
  cache.delete(key);
  if (current.state === 'done' && current.key === key) set({ state: 'idle' });
}
