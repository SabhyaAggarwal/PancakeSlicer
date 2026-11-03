import { useSyncExternalStore } from 'react';

export type Ev = { type: string };
type Of<E extends Ev, T> = Extract<E, { type: T }>;

export type Action<C, E extends Ev, S extends string> = (ctx: C, ev: E, m: Machine<S, E, C>) => Partial<C> | void;

export interface Transition<S extends string, C, E extends Ev> {
  target?: S;
  guard?: (ctx: C, ev: E) => boolean;
  action?: Action<C, E, S>;
}
type To<S extends string, C, E extends Ev> = S | Transition<S, C, E> | readonly Transition<S, C, E>[];

export interface StateNode<S extends string, E extends Ev, C> {
  on?: { [T in E['type']]?: To<S, C, Of<E, T>> };
  entry?: Action<C, E, S>;
  exit?: Action<C, E, S>;
  after?: { ms: number | ((ctx: C) => number) } & Transition<S, C, E>;
  note?: string;
}

export interface MachineDef<S extends string, E extends Ev, C> {
  id: string;
  initial: S;
  context: C;
  states: Record<S, StateNode<S, E, C>>;
  strict?: boolean;
}

export interface Snapshot<S extends string, C> { value: S; context: C }

export interface Machine<S extends string, E extends Ev, C> {
  readonly def: MachineDef<S, E, C>;
  get(): Snapshot<S, C>;
  send(ev: E): void;
  can(ev: E): boolean;
  subscribe(l: (s: Snapshot<S, C>) => void): () => void;
}

const list = <T>(t: T | readonly T[] | undefined): readonly T[] => (t === undefined ? [] : Array.isArray(t) ? t : [t as T]);
const norm = <S extends string, C, E extends Ev>(t: S | Transition<S, C, E>): Transition<S, C, E> => (typeof t === 'string' ? { target: t } : t);

export function assertNever(value: never): never {
  throw new Error(`Unhandled machine variant: ${String(value)}`);
}

export interface TransitionRecord {
  seq: number; at: number; machine: string; instance: number;
  from: string; event: string; to: string; accepted: boolean;
}
const LOG_LIMIT = 128;
const LOG_MACHINES = 64;
const logs = new Map<number, TransitionRecord[]>();
let instanceSerial = 0, transitionSerial = 0;
export function fsmLog(): TransitionRecord[] {
  return [...logs.values()].flat().sort((a, b) => a.seq - b.seq).map((r) => ({ ...r }));
}
declare global { interface Window { __fsmLog: typeof fsmLog } }
if (typeof window !== 'undefined') window.__fsmLog = fsmLog;
function record(instance: number, machine: string, from: string, event: string, to: string, accepted: boolean) {
  let ring = logs.get(instance);
  if (!ring) {
    if (logs.size >= LOG_MACHINES) logs.delete(logs.keys().next().value!);
    logs.set(instance, (ring = []));
  }
  const row = { seq: ++transitionSerial, at: Date.now(), machine, instance, from, event, to, accepted };
  if (ring.length === LOG_LIMIT) ring.shift();
  ring.push(row);
  try { if (typeof localStorage !== 'undefined' && localStorage.getItem('fsm-debug')) console.debug('[fsm]', row); }
  catch {  }
}

export function createMachine<S extends string, E extends Ev, C>(def: MachineDef<S, E, C>): Machine<S, E, C> {
  const instance = ++instanceSerial;
  let snap: Snapshot<S, C> = { value: def.initial, context: def.context };
  let timer: ReturnType<typeof setTimeout> | undefined;
  let busy = false;
  const queue: E[] = [];
  const listeners = new Set<(s: Snapshot<S, C>) => void>();
  const declared = new Set(Object.values<StateNode<S, E, C>>(def.states).flatMap((n) => Object.keys(n.on ?? {})));

  const run = (a: Action<C, E, S> | undefined, ev: E) => {
    const patch = a?.(snap.context, ev, m);
    if (patch) snap = { ...snap, context: { ...snap.context, ...patch } };
  };
  const pick = (ev: E) => list(def.states[snap.value].on?.[ev.type as E['type']] as To<S, C, E>)
    .map(norm).find((t) => !t.guard || t.guard(snap.context, ev));

  function take(t: Transition<S, C, E>, ev: E) {
    if (t.target === undefined) return run(t.action, ev);
    clearTimeout(timer);
    run(def.states[snap.value].exit, ev);
    run(t.action, ev);
    snap = { ...snap, value: t.target };
    const node = def.states[t.target];
    run(node.entry, ev);
    const after = node.after;
    if (after && snap.value === t.target) {
      const ms = typeof after.ms === 'function' ? after.ms(snap.context) : after.ms;
      timer = setTimeout(() => m.send({ type: '@after' } as E), ms);
    }
  }

  const m: Machine<S, E, C> = {
    def,
    get: () => snap,
    can: (ev) => !!pick(ev),
    send(ev) {
      queue.push(ev);
      if (busy) return;
      busy = true;
      const before = snap;
      try {
        for (let e; (e = queue.shift()); ) {
          const after = def.states[snap.value].after;
          const t = e.type === '@after' ? (after && (!after.guard || after.guard(snap.context, e)) ? after : undefined) : pick(e);
          const from = snap.value;
          const accepted = !!t && (t.target === undefined || Object.hasOwn(def.states, t.target));
          const edge = Object.hasOwn(def.states[from].on ?? {}, e.type) || e.type === '@after';
          if (accepted) take(t!, e);
          else if (def.strict && !edge) console.error(`[fsm] ${def.id}: ${declared.has(e.type) ? e.type : '@rejected'} is illegal in ${from}`);
          record(instance, def.id, from, edge || declared.has(e.type) ? e.type : '@rejected', snap.value, accepted);
        }
      } finally {
        busy = false;
      }
      if (snap !== before) listeners.forEach((l) => l(snap));
    },
    subscribe: (l) => (listeners.add(l), () => void listeners.delete(l)),
  };
  return m;
}

export function useMachine<S extends string, E extends Ev, C, T = Snapshot<S, C>>(
  m: Machine<S, E, C>, sel: (s: Snapshot<S, C>) => T = (s) => s as unknown as T,
): T {
  return useSyncExternalStore(m.subscribe, () => sel(m.get()));
}
