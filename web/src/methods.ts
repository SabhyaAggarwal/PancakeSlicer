import { useSyncExternalStore } from 'react';
import { plateJob, runJob } from './actions';
import { slice, summarize } from './flow/slice';
import type { Config } from './api/types';
import { MODE_LABELS } from './nonplanar-mode';
import { methodAvailability } from './profiles/method-context';
import { methodSelectionProblem } from './profiles/capabilities';
import { plateSignature } from './slice-input';
import { undoable } from './state/history';
import { effectiveConfig, getState, setState, type JobView, type State } from './state/store';

const METHODS = ['off', 'nonplanar_top', 'conformal_layers', 'smooth_morph', 'slant'];
const KEY = 'nonplanar_mode';

export type MethodRun = { method: string; label: string } & (
  | { state: 'waiting' }
  | { state: 'running'; pct: number; queued: boolean }
  | { state: 'done'; job: JobView }
  | { state: 'failed'; error: string }
  | { state: 'canceled' });

export interface MethodsSession { runs: MethodRun[]; original: JobView }

let session: MethodsSession | null = null;
let stops: (() => void)[] = [];
let generation = 0;
const listeners = new Set<() => void>();
const emit = () => { for (const l of listeners) l(); };
const update = (method: string, run: Partial<MethodRun>) => {
  if (!session) return;
  session = { ...session, runs: session.runs.map((r) => (r.method === method ? { ...r, ...run } as MethodRun : r)) };
  emit();
};

export function useMethods(): MethodsSession | null {
  return useSyncExternalStore((l) => (listeners.add(l), () => void listeners.delete(l)), () => session);
}

const without = (c: Config | undefined) => {
  if (!c || !(KEY in c)) return c;
  const { [KEY]: _, ...rest } = c;
  return rest;
};

function withMethod(s: State, method: string): Partial<State> {
  const unedited = effectiveConfig({ ...s, dirty: { ...s.dirty, process: {} } }, 'process');
  const process = String(unedited[KEY] ?? 'off') === method ? without(s.dirty.process)! : { ...s.dirty.process, [KEY]: method };
  return {
    dirty: { ...s.dirty, process },
    objects: s.objects.map((o) => o.settings[KEY] === undefined && !o.volumes?.some((v) => v.settings?.[KEY] !== undefined) && !o.ranges?.some((r) => r.settings[KEY] !== undefined) ? o : {
      ...o, settings: without(o.settings)!,
      ...(o.volumes && { volumes: o.volumes.map((v) => (v.settings ? { ...v, settings: without(v.settings)! } : v)) }),
      ...(o.ranges && { ranges: o.ranges.map((r) => ({ ...r, settings: without(r.settings)! })) }),
    }),
    projectPlates: s.projectPlates.map((p) => (p.index === s.plateIndex && KEY in p.settings ? { ...p, settings: without(p.settings)! } : p)),
  };
}

export function comparedMethods(s = getState()): string[] {
  const availability = methodAvailability(s);
  return METHODS.filter((m) => m !== 'slant' || availability.allowedModes.includes(m));
}

export async function sliceAllMethods() {
  stopAll();
  const s = getState(), api = s.api;
  if (!api || !s.objects.length) return;
  const mine = ++generation;
  session = { runs: comparedMethods(s).map((method) => ({ method, label: MODE_LABELS[method] ?? method, state: 'waiting' })), original: session?.original ?? s.job };
  emit();
  setState({ tab: 'preview' });
  const availability = methodAvailability(s);
  for (const { method } of session.runs) {
    if (mine !== generation) return;
    const next = { ...s, ...withMethod(s, method) } as State;
    const signature = plateSignature(next);
    const problem = methodSelectionProblem(availability, effectiveConfig(next, 'process'));
    if (problem) { update(method, { state: 'failed', error: problem.reason }); continue; }
    const job = getState().job;
    if (job.state === 'done' && !job.stale && job.plateSignature === signature) { finished(method, job); continue; }
    const request = await plateJob(next, false).catch((e: unknown) => String((e as Error)?.message ?? e));
    if (mine !== generation) return;
    if (request === null || typeof request === 'string') { update(method, { state: 'failed', error: request ?? 'Not sliced' }); continue; }
    update(method, { state: 'running', pct: 0, queued: true });
    await new Promise<void>((submitted) => {
      stops.push(runJob(api, { ...request, priority: 'low' }, signature, {
        progress: ({ id, pct, stage }) => { if (id) submitted(); if (mine === generation) update(method, { state: 'running', pct, queued: stage === 'queued' }); },
        done: (j) => { submitted(); if (mine === generation) finished(method, j); },
        failed: (error) => { submitted(); if (mine === generation) update(method, { state: 'failed', error }); },
      }));
    });
  }
}

function finished(method: string, job: JobView) {
  update(method, { state: 'done', job });
  const shown = getState().job;
  if (!(shown.state === 'done' && shown.status)) viewMethod(method);
}

function stopAll() {
  ++generation;
  for (const stop of stops) stop();
  stops = [];
}

export function cancelMethods() {
  stopAll();
  if (!session) return;
  session = { ...session, runs: session.runs.map((r) => (r.state === 'waiting' || r.state === 'running' ? { ...r, state: 'canceled' } as MethodRun : r)) };
  emit();
}

export function viewMethod(method: string) {
  const run = session?.runs.find((r) => r.method === method);
  if (run?.state === 'done') setState({ job: { ...run.job, stale: run.job.plateSignature !== plateSignature(getState()) } });
}

export function keepMethod(method: string) {
  const run = session?.runs.find((r) => r.method === method);
  if (run?.state !== 'done') return;
  undoable(`Slicing method ${run.label}`, () => setState((s) => withMethod(s, method)));
  const job = { ...run.job, stale: run.job.plateSignature !== plateSignature(getState()) };
  setState({ job });
  if (!job.stale) slice.send({ type: 'RESTORE', result: { signature: job.plateSignature!, job, summary: summarize(job), at: Date.now() } });
  close();
}

export function closeMethods() {
  const original = session?.original;
  if (original && getState().job.id !== original.id)
    setState({ job: original.state === 'done' ? { ...original, stale: original.plateSignature !== plateSignature(getState()) } : original });
  close();
}

function close() {
  stopAll();
  session = null;
  emit();
}
