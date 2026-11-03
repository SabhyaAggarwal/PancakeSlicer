import { plateJob, runJob } from '../actions';
import { flushReady } from '../flush-state';
import { plateSignature } from '../slice-input';
import { documentGeneration, subscribeHistory } from '../state/history';
import { getState, setState, subscribeStore, toast, type JobView, type State } from '../state/store';
import { createMachine, useMachine, type Machine, type Snapshot } from './fsm';

export interface SliceSummary {
  timeS: number | null;
  grams: number | null;
  supportG: number | null;
  layers: number | null;
  warnings: string[];
}

export interface SliceResult {
  signature: string;
  job: JobView;
  summary: SliceSummary;
  at: number;
}

export interface SliceDiff {
  timeS: number | null;
  grams: number | null;
  supportG: number | null;
  layers: number | null;
  supportGone: boolean;
  supportAdded: boolean;
  warningsAdded: string[];
  warningsCleared: string[];
}

export type SliceStateName = 'editing' | 'slicing' | 'sliced' | 'stale' | 'failed';

export interface SliceContext {
  result: SliceResult | null;
  previous: SliceResult | null;
  target: string | null;
  pct: number;
  stage: string;
  error: string | null;
}

export type SliceEvent =
  | { type: 'CHANGE' }
  | { type: 'RESET' }
  | { type: 'CANCEL' }
  | { type: 'START'; signature: string }
  | { type: 'RESTORE'; result: SliceResult }
  | { type: 'PROGRESS'; pct: number; stage: string }
  | { type: 'DONE'; result: SliceResult }
  | { type: 'ERROR'; error: string }
  | { type: 'ANNOTATE'; signature: string; part: Partial<SliceSummary> };

const CACHE_SIZE = 12;
const CACHE_MS = 100 * 60_000;

const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : typeof v === 'string' && v.trim() && Number.isFinite(+v) ? +v : null);

export function summarize(job: JobView): SliceSummary {
  const r = (job.status?.record ?? {}) as Record<string, Record<string, unknown> | undefined>;
  return {
    timeS: num(r.totals?.estimated_time_s) ?? num(r.text_summary?.estimated_time_s),
    grams: num(r.statistics?.total_weight_g),
    supportG: num(r.statistics?.support_weight_g),
    layers: num(r.totals?.layer_count) ?? num(r.statistics?.layer_count),
    warnings: job.warnings ?? [],
  };
}

const cache = new Map<string, SliceResult>();
function keep(r: SliceResult) {
  cache.delete(r.signature);
  cache.set(r.signature, r);
  for (const [k, old] of cache) {
    if (cache.size <= CACHE_SIZE) break;
    cache.delete(k);
    const ctx = slice.get().context;
    if (old.job.id && old !== ctx.result && old !== ctx.previous) void getState().api?.cancel(old.job.id).catch(() => undefined);
  }
}
const cached = (sig: string) => {
  const r = cache.get(sig);
  if (r && Date.now() - r.at > CACHE_MS) return void cache.delete(sig);
  return r;
};

const ALWAYS: (keyof State)[] = ['objects', 'projectPlates', 'plateIndex', 'project', 'events', 'filamentSlots', 'slotColours', 'flush',
  'printer', 'process', 'filamentEdits', 'bedType', 'wipeTowerPos', 'mixes'];
const NEVER = new Set<keyof State>(['job', 'tab', 'selected', 'toast', 'toastSeq', 'toastAction', 'toastMs']);
let watched: (keyof State)[] = ALWAYS;
let seen: State = getState();

function signature(s = getState()): string {
  const read = new Set<keyof State>(ALWAYS);
  const sig = plateSignature(new Proxy(s, { get: (t, k, r) => (read.add(k as keyof State), Reflect.get(t, k, r)) }));
  watched = [...read].filter((k) => !NEVER.has(k));
  return sig;
}

let cancel: (() => void) | null = null;
let token = 0;
async function start(sig: string, m: Machine<SliceStateName, SliceEvent, SliceContext>) {
  const mine = ++token;
  const s = getState();
  if (!m.get().context.result) setState({ job: { state: 'submitting', pct: 0, stage: 'Preparing', plateSignature: sig } });
  const request = await plateJob(s, false).catch((e: unknown) => String(e));
  if (mine !== token) return;
  if (request === null || typeof request === 'string') return m.send({ type: 'ERROR', error: request ?? 'Not sliced' });
  cancel = runJob(s.api!, request, sig, {
    progress: (p) => {
      if (mine !== token) return;
      m.send({ type: 'PROGRESS', pct: p.id && p.stage === 'queued' ? 0 : p.pct, stage: p.stage });
      if (!m.get().context.result) setState((st) => ({ job: { ...st.job, ...p, state: p.id ? 'running' : st.job.state } }));
    },
    done: (job) => mine === token && m.send({ type: 'DONE', result: { signature: sig, job, summary: summarize(job), at: Date.now() } }),
    failed: (error) => mine === token && m.send({ type: 'ERROR', error }),
  });
}

function stop() {
  ++token;
  cancel?.();
  cancel = null;
}

function show(r: SliceResult | null) {
  const job = getState().job;
  if (r && (job.id !== r.job.id || job.stale || job.state !== r.job.state)) setState({ job: r.job });
}

const advance = (ctx: SliceContext, ev: SliceEvent) => {
  const r = (ev as { result: SliceResult }).result;
  return { previous: ctx.result ?? ctx.previous, result: r, error: null };
};
const annotated = (ctx: SliceContext, e: Extract<SliceEvent, { type: 'ANNOTATE' }>) => {
  const fill = (r: SliceResult | null) => (r?.signature === e.signature ? { ...r, summary: { ...r.summary, ...e.part } } : r);
  const r = cache.get(e.signature);
  if (r) cache.set(e.signature, fill(r)!);
  return { result: fill(ctx.result), previous: fill(ctx.previous) };
};
const ANNOTATE = { action: annotated };
const changed = [
  { target: 'stale', guard: (ctx: SliceContext) => !!ctx.result },
  { target: 'editing' },
] satisfies { target: SliceStateName; guard?: (ctx: SliceContext) => boolean }[];
const common = {
  CHANGE: changed,
  CANCEL: changed,
  RESET: { target: 'editing', action: () => ({ result: null, previous: null, target: null, pct: 0, stage: '', error: null }) },
  START: { target: 'slicing', action: (_: SliceContext, e: { signature: string }) => ({ target: e.signature, pct: 0, stage: 'Preparing', error: null }) },
  RESTORE: { target: 'sliced', action: advance },
  ANNOTATE,
} as const;

export const slice = createMachine<SliceStateName, SliceEvent, SliceContext>({
  id: 'slice', initial: 'editing',
  context: { result: null, previous: null, target: null, pct: 0, stage: '', error: null },
  states: {
    editing: { note: 'not sliced', on: common },
    stale: { note: 'out of date; last result remains visible', on: common },
    slicing: {
      entry: (ctx, _e, m) => void start(ctx.target!, m),
      exit: () => stop(),
      on: { ...common,
        PROGRESS: { action: (_, e) => ({ pct: e.pct, stage: e.stage }) },
        DONE: { target: 'sliced', action: advance },
        ERROR: { target: 'failed', action: (_, e) => ({ error: e.error }) },
      },
    },
    sliced: { entry: (ctx) => show(ctx.result), on: common },
    failed: {
      entry: (ctx) => {
        if (ctx.result) toast(ctx.error!.split('\n')[0]);
        else setState({ job: { state: 'failed', pct: 0, stage: '', error: ctx.error! } });
      },
      on: common,
    },
  },
});

let wired = false;
export function startSliceTracking() {
  if (wired) return;
  wired = true;
  let generation = documentGeneration();
  seen = getState();
  let lastSignature = signature(seen);
  subscribeHistory(() => {
    if (generation === documentGeneration()) return;
    generation = documentGeneration();
    lastSignature = signature();
    slice.send({ type: 'RESET' });
  });
  subscribeStore(() => {
    const s = getState(), was = seen;
    seen = s;
    if (s.job !== was.job && s.job.state === 'done' && !s.job.calib && s.job.plateSignature &&
        s.job.id !== slice.get().context.result?.job.id && s.job.id !== was.job.id) {
      const r: SliceResult = { signature: s.job.plateSignature, job: s.job, summary: summarize(s.job), at: Date.now() };
      keep(r);
      if (!s.job.stale && r.signature === signature(s)) slice.send({ type: 'RESTORE', result: r });
    }
    if (watched.some((k) => s[k] !== was[k])) {
      const sig = signature(s);
      if (sig !== lastSignature) { lastSignature = sig; slice.send({ type: 'CHANGE' }); }
    }
  });
  slice.subscribe((snap) => { if (snap.context.result && snap.value === 'sliced') keep(snap.context.result); });
}

let requested = 0;
export async function sliceNow() {
  const mine = ++requested, generation = documentGeneration();
  await flushReady();
  if (mine !== requested || generation !== documentGeneration()) return;
  const s = getState();
  if (!s.api || !s.objects.length) return;
  const sig = signature(s);
  const hit = cached(sig);
  if (hit) return slice.send({ type: 'RESTORE', result: hit });
  slice.send({ type: 'START', signature: sig });
}

export function cancelSlice() {
  ++requested;
  slice.send({ type: 'CANCEL' });
  const result = slice.get().context.result;
  setState({ job: result ? { ...result.job, stale: true } : { state: 'canceled', pct: 0, stage: '' } });
}
export const cancelAutoSlice = cancelSlice;

export type SliceSnapshot = Snapshot<SliceStateName, SliceContext>;
export function useSlice(): SliceSnapshot;
export function useSlice<T>(sel: (s: SliceSnapshot) => T): T;
export function useSlice<T>(sel?: (s: SliceSnapshot) => T) {
  return useMachine(slice, sel);
}

export const currentResult = (): SliceResult | null => slice.get().context.result;
export const isStale = (s: SliceSnapshot = slice.get()) => !!s.context.result && s.value !== 'sliced';
export const isFresh = (s: SliceSnapshot = slice.get()) => s.value === 'sliced';

const minus = (a: number | null, b: number | null) => (a == null || b == null ? null : a - b);
let memo: { a: SliceResult; b: SliceResult; d: SliceDiff } | null = null;
export function sliceDiff(ctx: SliceContext = slice.get().context): SliceDiff | null {
  const { result: a, previous: b } = ctx;
  if (!a || !b) return null;
  if (memo?.a === a && memo.b === b) return memo.d;
  const x = a.summary, y = b.summary;
  const d: SliceDiff = {
    timeS: minus(x.timeS, y.timeS), grams: minus(x.grams, y.grams), supportG: minus(x.supportG, y.supportG), layers: minus(x.layers, y.layers),
    supportGone: !!y.supportG && x.supportG === 0, supportAdded: y.supportG === 0 && !!x.supportG,
    warningsAdded: x.warnings.filter((w) => !y.warnings.includes(w)), warningsCleared: y.warnings.filter((w) => !x.warnings.includes(w)),
  };
  memo = { a, b, d };
  return d;
}

let waiting: (() => void) | null = null;
export function whenFresh(f: () => void) {
  waiting?.(); waiting = null;
  if (isFresh()) return f();
  if (slice.get().value !== 'slicing') return;
  const off = slice.subscribe((s) => {
    if (s.value === 'slicing') return;
    off(); waiting = null;
    if (isFresh(s)) f();
  });
  waiting = off;
}

export const annotate = (signature: string, part: Partial<Pick<SliceSummary, 'supportG' | 'layers'>>) => slice.send({ type: 'ANNOTATE', signature, part });

(window as unknown as { __slice: unknown }).__slice = { machine: slice, cache, sliceDiff, signature };
