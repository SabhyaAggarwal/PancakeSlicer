import { createElement, lazy, type ComponentProps, type ComponentType } from 'react';

const queue: Array<() => Promise<unknown>> = [];
let started = false, running = 0;
const LANES = 3;

// eslint-disable-next-line @typescript-eslint/no-explicit-any -- the component type lazy() itself accepts
export function preloadable<T extends ComponentType<any>>(load: () => Promise<{ default: T }>, { idle = true } = {}) {
  let loaded: T | null = null;
  let pending: Promise<{ default: T }> | null = null;
  const preload = () => pending ??= load().then((m) => { loaded = m.default; return m; }, (error: unknown) => { pending = null; throw error; });
  const Lazy = lazy(preload);
  const View = (props: ComponentProps<T>) => createElement((loaded ?? Lazy) as ComponentType<ComponentProps<T>>, props);
  if (idle) { queue.push(preload); drain(); }
  return Object.assign(View, { preload });
}

export function preloadViews() {
  started = true;
  drain();
}
function drain() {
  if (!started) return;
  while (running < LANES && queue.length) {
    const load = queue.shift()!;
    running++;
    whenIdle(() => void load().catch(() => {}).finally(() => { running--; drain(); }));
  }
}

export function whenIdle(task: () => void, timeout = 3000) {
  if ('requestIdleCallback' in window) requestIdleCallback(() => task(), { timeout });
  else setTimeout(task, 1500);
}
