import { observePreference, preferenceJson, savePreferences } from '../saved-preferences';
import { useSyncExternalStore } from 'react';

export interface ViewOptions { gridlines: boolean; labels: boolean; overhang: boolean; outline: boolean; edges: boolean; navigator: boolean; gcodeWindow: boolean; wireframe: boolean; xray: boolean; axes: boolean; com: boolean }
const KEY = 'orca.web.view';
const saved: Partial<ViewOptions> = {};
let options: ViewOptions = { gridlines: saved.gridlines !== false, labels: false, overhang: false, outline: saved.outline === true, edges: saved.edges !== false, wireframe: false, xray: false, axes: saved.axes !== false, com: saved.com === true,
  navigator: saved.navigator !== false, gcodeWindow: saved.gcodeWindow === true };
const listeners = new Set<() => void>();
export const viewOptions = () => options;
export const subscribeViewOptions = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export const useViewOptions = () => useSyncExternalStore(subscribeViewOptions, viewOptions);
export function toggleViewOption(k: keyof ViewOptions) {
  options = { ...options, [k]: !options[k] };
  savePreferences({[KEY]: { gridlines: options.gridlines, outline: options.outline, edges: options.edges, axes: options.axes, com: options.com, navigator: options.navigator, gcodeWindow: options.gcodeWindow }});
  listeners.forEach((l) => l());
}

observePreference(KEY, () => {
  const saved = preferenceJson<Partial<ViewOptions>>(KEY, {});
  options = { ...options, gridlines: saved.gridlines !== false, outline: saved.outline === true, edges: saved.edges !== false, axes: saved.axes !== false, com: saved.com === true, navigator: saved.navigator !== false, gcodeWindow: saved.gcodeWindow === true };
  listeners.forEach(listener => listener());
});
