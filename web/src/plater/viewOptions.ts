import { useSyncExternalStore } from 'react';

export interface ViewOptions { gridlines: boolean; labels: boolean; overhang: boolean; outline: boolean }
const KEY = 'orca.web.view';
const saved = (() => { try { return JSON.parse(localStorage.getItem(KEY) ?? '{}'); } catch { return {}; } })();
let options: ViewOptions = { gridlines: saved.gridlines !== false, labels: false, overhang: false, outline: saved.outline === true };
const listeners = new Set<() => void>();
export const viewOptions = () => options;
export const subscribeViewOptions = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export const useViewOptions = () => useSyncExternalStore(subscribeViewOptions, viewOptions);
export function toggleViewOption(k: keyof ViewOptions) {
  options = { ...options, [k]: !options[k] };
  localStorage.setItem(KEY, JSON.stringify({ gridlines: options.gridlines, outline: options.outline }));
  listeners.forEach((l) => l());
}
