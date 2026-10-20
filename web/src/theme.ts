export type ThemePref = 'system' | 'light' | 'dark';

const KEY = 'orca.web.theme';
const media = window.matchMedia('(prefers-color-scheme: dark)');
const listeners = new Set<() => void>();
let pref: ThemePref = 'system';
let dark = false;
try {
  const stored = localStorage.getItem(KEY);
  if (stored === 'light' || stored === 'dark') pref = stored;
} catch {  }

function apply(changed = false) {
  const next = pref === 'dark' || (pref === 'system' && media.matches);
  document.documentElement.dataset.theme = next ? 'dark' : 'light';
  if (next === dark && !changed) return;
  dark = next;
  listeners.forEach((l) => l());
}

media.addEventListener('change', () => apply());
apply();

export const themePref = () => pref;
export const isDark = () => dark;

export function setThemePref(p: ThemePref) {
  pref = p;
  try {
    if (p === 'system') localStorage.removeItem(KEY);
    else localStorage.setItem(KEY, p);
  } catch {  }
  apply(true);
}

export function subscribeTheme(l: () => void) {
  listeners.add(l);
  return () => void listeners.delete(l);
}
