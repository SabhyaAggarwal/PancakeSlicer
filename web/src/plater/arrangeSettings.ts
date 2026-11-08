import { observePreference, preferenceJson, savePreferences } from '../saved-preferences';
const KEY = 'orca.arrange';

export interface ArrangeSettings { distance: number; rotation: boolean; multiMaterials: boolean }
export const ARRANGE_DEFAULTS: ArrangeSettings = { distance: 0, rotation: false, multiMaterials: true };

let settings: ArrangeSettings = { ...ARRANGE_DEFAULTS };
const listeners = new Set<() => void>();

export const arrangeSettings = () => settings;
export function setArrangeSettings(patch: Partial<ArrangeSettings>) {
  settings = { ...settings, ...patch };
  settings.distance = Math.min(100, Math.max(0, Number(settings.distance) || 0));
  savePreferences({[KEY]: {...settings}});
  listeners.forEach((l) => l());
}
export function resetArrangeSettings() {
  settings = { ...ARRANGE_DEFAULTS };
  savePreferences({[KEY]: {...ARRANGE_DEFAULTS}});
  listeners.forEach((l) => l());
}
export const subscribeArrangeSettings = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));

observePreference(KEY, () => {
  const saved = preferenceJson<Partial<ArrangeSettings>>(KEY, {});
  settings = { distance: Math.min(100, Math.max(0, Number(saved.distance) || 0)), rotation: saved.rotation === true, multiMaterials: saved.multiMaterials !== false };
  listeners.forEach(listener => listener());
});
