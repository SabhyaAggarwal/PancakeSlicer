import { useSyncExternalStore } from 'react';

export type Ev = { type: string };
type Of<E extends Ev, T> = Extract<E, { type: T }>;

export type Action<C, E extends Ev, S extends string> = (ctx: C, ev: E, m: Machine<S, E, C>) => Partial<C> | void;

export interface Transition<S extends string, C, E extends Ev> {
  target?: S;
  guard?: (ctx: C, ev: E) => boolean;
  action?: Action<C, E, S>;
}
type To<S extends string, C, E extends Ev> = S | Transition<S, C, E> | Transition<S, C, E>[];

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
}

export interface Snapshot<S extends string, C> { value: S; context: C }

export interface Machine<S extends string, E extends Ev, C> {
  readonly def: MachineDef<S, E, C>;
  get(): Snapshot<S, C>;
  send(ev: E): void;
  can(ev: E): boolean;
  subscribe(l: (s: Snapshot<S, C>) => void): () => void;
}

const list = <T>(t: T | T[] | undefined): T[] => (t === undefined ? [] : Array.isArray(t) ? t : [t]);
const norm = <S extends string, C, E extends Ev>(t: S | Transition<S, C, E>): Transition<S, C, E> => (typeof t === 'string' ? { target: t } : t);

export function createMachine<S extends string, E extends Ev, C>(def: MachineDef<S, E, C>): Machine<S, E, C> {
  let snap: Snapshot<S, C> = { value: def.initial, context: def.context };
  let timer: ReturnType<typeof setTimeout> | undefined;
  let busy = false;
  const queue: E[] = [];
  const listeners = new Set<(s: Snapshot<S, C>) => void>();

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
          if (t) take(t, e);
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
