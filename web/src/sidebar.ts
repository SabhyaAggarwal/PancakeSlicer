import { useSyncExternalStore } from 'react';

const KEY = 'orca.web.sidebar';
const listeners = new Set<() => void>();
let hidden = false;
try {
  hidden = localStorage.getItem(KEY) === 'hidden';
} catch {  }

export const sidebarHidden = () => hidden;

export function toggleSidebar(next = !hidden) {
  hidden = next;
  try {
    if (hidden) localStorage.setItem(KEY, 'hidden');
    else localStorage.removeItem(KEY);
  } catch {  }
  listeners.forEach((l) => l());
}

const subscribe = (l: () => void) => { listeners.add(l); return () => void listeners.delete(l); };
export const useSidebarHidden = () => useSyncExternalStore(subscribe, sidebarHidden);

export const RAIL_MIN = 280, RAIL_MAX = 520, RAIL_DEFAULT = 360;
const WIDTH_KEY = 'orca.web.railWidth';
let width = RAIL_DEFAULT;
try { width = Math.min(RAIL_MAX, Math.max(RAIL_MIN, Number(localStorage.getItem(WIDTH_KEY)) || RAIL_DEFAULT)); } catch {  }
export function setRailWidth(next: number, persist = true) {
  width = Math.round(Math.min(RAIL_MAX, Math.max(RAIL_MIN, next)));
  if (persist) try { localStorage.setItem(WIDTH_KEY, String(width)); } catch {  }
  listeners.forEach((l) => l());
}
export const useRailWidth = () => useSyncExternalStore(subscribe, () => width);

export type RailProfile = 'machine' | 'filament' | 'process';
let profile: RailProfile = 'process';
export const setRailProfile = (next: RailProfile) => { profile = next; listeners.forEach((l) => l()); };
export const useRailProfile = () => useSyncExternalStore(subscribe, () => profile);

export const OBJECTS_ROW = 24, OBJECTS_MIN = OBJECTS_ROW * 2, OBJECTS_MAX = OBJECTS_ROW * 12, OBJECTS_DEFAULT = OBJECTS_ROW * 4;
const OBJECTS_KEY = 'orca.web.railObjects';
let objectsHeight = OBJECTS_DEFAULT;
try { objectsHeight = Math.min(OBJECTS_MAX, Math.max(OBJECTS_MIN, Number(localStorage.getItem(OBJECTS_KEY)) || OBJECTS_DEFAULT)); } catch {  }
export function setObjectsHeight(next: number, persist = true) {
  objectsHeight = Math.round(Math.min(OBJECTS_MAX, Math.max(OBJECTS_MIN, next)));
  if (persist) try { localStorage.setItem(OBJECTS_KEY, String(objectsHeight)); } catch {  }
  listeners.forEach((l) => l());
}
export const useObjectsHeight = () => useSyncExternalStore(subscribe, () => objectsHeight);
