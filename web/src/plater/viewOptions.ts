import { useSyncExternalStore } from 'react';

export interface ViewOptions { gridlines: boolean; labels: boolean; overhang: boolean; outline: boolean; navigator: boolean; gcodeWindow: boolean; wireframe: boolean; xray: boolean; axes: boolean; com: boolean }
const KEY = 'orca.web.view';
const saved = (() => { try { return JSON.parse(localStorage.getItem(KEY) ?? '{}'); } catch { return {}; } })();
let options: ViewOptions = { gridlines: saved.gridlines !== false, labels: false, overhang: false, outline: saved.outline === true, wireframe: false, xray: false, axes: saved.axes !== false, com: saved.com === true,
  navigator: saved.navigator !== false, gcodeWindow: saved.gcodeWindow === true };
const listeners = new Set<() => void>();
export const viewOptions = () => options;
export const subscribeViewOptions = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export const useViewOptions = () => useSyncExternalStore(subscribeViewOptions, viewOptions);
export function toggleViewOption(k: keyof ViewOptions) {
  options = { ...options, [k]: !options[k] };
  try {
    localStorage.setItem(KEY, JSON.stringify({ gridlines: options.gridlines, outline: options.outline, axes: options.axes, com: options.com, navigator: options.navigator, gcodeWindow: options.gcodeWindow }));
  } catch {  }
  listeners.forEach((l) => l());
}
