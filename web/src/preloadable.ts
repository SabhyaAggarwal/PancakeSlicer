import { createElement, lazy, type ComponentType } from 'react';

export function preloadable<P extends object>(load: () => Promise<{ default: ComponentType<P> }>) {
  let loaded: ComponentType<P> | null = null;
  let pending: Promise<{ default: ComponentType<P> }> | null = null;
  const preload = () => pending ??= load().then((m) => { loaded = m.default; return m; }, (error: unknown) => { pending = null; throw error; });
  const Lazy = lazy(preload);
  const View = (props: P) => createElement(loaded ?? Lazy, props);
  return Object.assign(View, { preload });
}

export function whenIdle(task: () => void, timeout = 3000) {
  if ('requestIdleCallback' in window) requestIdleCallback(() => task(), { timeout });
  else setTimeout(task, 1500);
}
