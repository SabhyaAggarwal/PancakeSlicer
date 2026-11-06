





import { useSyncExternalStore } from 'react';
import { annotate } from '../flow/slice';
import { getState, setState, subscribeStore } from '../state/store';
import { MoveType } from './format';
import type { PreviewData } from './data';


export const VOXEL_MM = 2;

const KEEP = 12;


const SUPPORT_ROLES = (1 << 14) | (1 << 15) | (1 << 16);




const SHELL_ROLES = [1, 2, 3, 6, 7, 8, 9, 11, 12, 13, 17].reduce((m, r) => m | (1 << r), 0);

export interface ResultNumbers {

  time: number | null;
  grams: number | null;
  supportGrams: number | null;
  layers: number | null;
}


export interface Occupancy {
  support: Set<number>;
  shell: Set<number>;
}

export interface SliceResult {

  id: string;

  signature?: string;

  at: number;
  record?: Record<string, unknown>;
  numbers: ResultNumbers;
  occupancy?: Occupancy;
}

let results: SliceResult[] = [];
const listeners = new Set<() => void>();
const emit = () => {
  results = [...results];
  for (const l of listeners) l();
};
const subscribe = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));


export const getResults = () => results;
export const getResult = (id: string | undefined) => (id ? results.find((r) => r.id === id) : undefined);
export function useResults(): SliceResult[] {
  return useSyncExternalStore(subscribe, () => results);
}

const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : typeof v === 'string' && v.trim() !== '' && Number.isFinite(Number(v)) ? Number(v) : null);

function recordNumbers(record: Record<string, unknown> | undefined): Pick<ResultNumbers, 'time' | 'grams'> {
  const r = record as { text_summary?: Record<string, unknown>; totals?: Record<string, unknown>; statistics?: Record<string, unknown> } | undefined;
  return { time: num(r?.totals?.estimated_time_s) ?? num(r?.text_summary?.estimated_time_s), grams: num(r?.statistics?.total_weight_g) };
}
const recordSupport = (record: Record<string, unknown> | undefined) => num((record as { statistics?: Record<string, unknown> } | undefined)?.statistics?.support_weight_g);






export function noteResult(id: string, record: Record<string, unknown> | undefined, signature?: string) {
  const known = getResult(id);
  if (known) {
    if (record && !known.record) {
      known.record = record;
      known.numbers = { ...known.numbers, ...recordNumbers(record) };
    }
    if (results[results.length - 1] !== known) results = [...results.filter((r) => r !== known), known];
    emit();
    return;
  }
  results.push({ id, signature, at: Date.now(), record, numbers: { ...recordNumbers(record), supportGrams: null, layers: null } });
  if (results.length > KEEP) results.splice(0, results.length - KEEP);
  emit();
}


let lastId: string | undefined;
subscribeStore(() => {
  const j = getState().job;
  if (j.state === 'done' && j.id && j.id !== lastId) {
    lastId = j.id;
    noteResult(j.id, j.status?.record, j.plateSignature);
  }
});
{
  const j = getState().job;
  if (j.state === 'done' && j.id) noteResult((lastId = j.id), j.status?.record, j.plateSignature);
}

const key = (x: number, y: number, z: number) => {

  const ix = Math.floor(x / VOXEL_MM) + 500, iy = Math.floor(y / VOXEL_MM) + 500, iz = Math.floor(z / VOXEL_MM);
  return (iz * 1000 + iy) * 1000 + ix;
};
export const voxelCentre = (k: number): [number, number, number] => {
  const ix = k % 1000, iy = Math.floor(k / 1000) % 1000, iz = Math.floor(k / 1e6);
  return [(ix - 500 + 0.5) * VOXEL_MM, (iy - 500 + 0.5) * VOXEL_MM, (iz + 0.5) * VOXEL_MM];
};
export const vertexKey = (d: PreviewData, i: number) => key(d.pos[i * 4], d.pos[i * 4 + 1], d.pos[i * 4 + 2]);


export function occupy(d: PreviewData, occ: Occupancy, from: number, to: number) {
  for (let i = Math.max(1, from); i < to; ++i) {
    if ((d.type[i] & 0x7f) !== MoveType.Extrude) continue;
    const bit = 1 << d.role[i];
    if (bit & SUPPORT_ROLES) occ.support.add(vertexKey(d, i));
    else if (bit & SHELL_ROLES) occ.shell.add(vertexKey(d, i));
  }
}


export function noteStream(id: string, d: PreviewData, occupancy: Occupancy) {
  const r = getResult(id);
  if (!r || !d.done) return;
  const total = r.numbers.grams;
  const mm = d.roleFilament[14] + d.roleFilament[15] + d.roleFilament[16];
  const all = d.filamentMm.reduce((a, b) => a + (b ?? 0), 0);
  r.numbers = {
    ...r.numbers,
    time: r.numbers.time ?? d.totalTime(),
    layers: d.layers.length,
    supportGrams: recordSupport(r.record) ?? (mm <= 0 ? 0 : total != null && all > 0 ? (total * mm) / all : null),
  };
  r.occupancy = occupancy;
  emit();
  if (r.signature) annotate(r.signature, { supportG: r.numbers.supportGrams, layers: r.numbers.layers });
}

export interface ResultDiff {
  from: SliceResult;
  to: SliceResult;

  time: number | null;
  grams: number | null;
  supportGrams: number | null;
  layers: number | null;

  added?: Set<number>;
  removed?: Set<number>;
}

const delta = (a: number | null, b: number | null) => (a == null || b == null ? null : b - a);
const minus = (a: Set<number>, b: Set<number>, out: Set<number>) => {
  for (const k of a) if (!b.has(k)) out.add(k);
  return out;
};

const diffCache = new WeakMap<SliceResult, WeakMap<SliceResult, ResultDiff>>();

export function diffResults(from: SliceResult, to: SliceResult): ResultDiff {
  const hit = diffCache.get(to)?.get(from);
  if (hit && (hit.added || !from.occupancy || !to.occupancy)) return hit;
  const a = from.numbers, b = to.numbers;
  const d: ResultDiff = { from, to, time: delta(a.time, b.time), grams: delta(a.grams, b.grams), supportGrams: delta(a.supportGrams, b.supportGrams), layers: delta(a.layers, b.layers) };
  if (from.occupancy && to.occupancy) {
    const f = from.occupancy, t = to.occupancy;
    d.added = minus(t.shell, f.shell, minus(t.support, f.support, new Set()));
    d.removed = minus(f.shell, t.shell, minus(f.support, t.support, new Set()));
  }
  let m = diffCache.get(to);
  if (!m) diffCache.set(to, (m = new WeakMap()));
  m.set(from, d);
  return d;
}


export function latestDiff(list = results): ResultDiff | null {
  return list.length < 2 ? null : diffResults(list[list.length - 2], list[list.length - 1]);
}
export function useLatestDiff(): ResultDiff | null {
  return latestDiff(useResults());
}


export function signed(v: number | null, unit: 'time' | 'g' | 'layers'): string | null {
  if (v == null) return null;
  const sign = v > 0 ? '+' : v < 0 ? '−' : '±';
  const a = Math.abs(v);
  if (unit === 'time') {
    if (a < 30) return '±0 min';
    const m = Math.round(a / 60);
    return m < 60 ? `${sign}${m} min` : `${sign}${Math.floor(m / 60)} h ${String(m % 60).padStart(2, '0')} min`;
  }
  if (unit === 'g') return a < 0.05 ? '±0 g' : `${sign}${a < 10 ? a.toFixed(1) : Math.round(a)} g`;
  return a === 0 ? '±0' : `${sign}${a}`;
}




let compareTarget: string | null = null;
const targetListeners = new Set<() => void>();
export function compareWith(resultId: string | null) {
  compareTarget = resultId && getResult(resultId) ? resultId : null;
  if (compareTarget && getState().tab !== 'preview') setState({ tab: 'preview' });
  for (const l of targetListeners) l();
}
export const getCompareTarget = () => compareTarget;
export function useCompareTarget(): SliceResult | null {
  const id = useSyncExternalStore((l) => (targetListeners.add(l), () => void targetListeners.delete(l)), () => compareTarget);
  const list = useResults();
  return list.find((r) => r.id === id) ?? null;
}
