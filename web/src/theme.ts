import { observePreference, preferenceText, savePreferences } from './saved-preferences';



export type ThemePref = 'system' | 'light' | 'dark';

const KEY = 'orca.web.theme';
const media = window.matchMedia('(prefers-color-scheme: dark)');
const listeners = new Set<() => void>();
let pref: ThemePref = 'dark';
let dark = false;


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
  savePreferences({[KEY]: p});
  apply(true);
}

export function subscribeTheme(l: () => void) {
  listeners.add(l);
  return () => void listeners.delete(l);
}

observePreference(KEY, () => { const saved = preferenceText(KEY); pref = saved === 'system' || saved === 'light' ? saved : 'dark'; apply(true); });
