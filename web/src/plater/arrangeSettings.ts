const KEY = 'orca.arrange';

export interface ArrangeSettings { distance: number; rotation: boolean }
export const ARRANGE_DEFAULTS: ArrangeSettings = { distance: 0, rotation: false };

let settings: ArrangeSettings = (() => {
  try { return { ...ARRANGE_DEFAULTS, ...JSON.parse(localStorage.getItem(KEY) ?? '{}') }; } catch { return { ...ARRANGE_DEFAULTS }; }
})();
const listeners = new Set<() => void>();

export const arrangeSettings = () => settings;
export function setArrangeSettings(patch: Partial<ArrangeSettings>) {
  settings = { ...settings, ...patch };
  settings.distance = Math.min(100, Math.max(0, Number(settings.distance) || 0));
  try { localStorage.setItem(KEY, JSON.stringify(settings)); } catch {  }
  listeners.forEach((l) => l());
}
export function resetArrangeSettings() {
  settings = { ...ARRANGE_DEFAULTS };
  try { localStorage.removeItem(KEY); } catch {  }
  listeners.forEach((l) => l());
}
export const subscribeArrangeSettings = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
