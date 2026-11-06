import { observePreference, preferenceText, savePreferences } from './saved-preferences';


import { useSyncExternalStore } from 'react';

const KEY = 'orca.web.sidebar';
const listeners = new Set<() => void>();
let hidden = false;


export const sidebarHidden = () => hidden;

export function toggleSidebar(next = !hidden) {
  hidden = next;
  savePreferences({[KEY]: hidden ? 'hidden' : null});
  listeners.forEach((l) => l());
}

const subscribe = (l: () => void) => { listeners.add(l); return () => void listeners.delete(l); };
export const useSidebarHidden = () => useSyncExternalStore(subscribe, sidebarHidden);



export const RAIL_MIN = 280, RAIL_MAX = 520, RAIL_DEFAULT = 360;
const WIDTH_KEY = 'orca.web.railWidth';
let width = RAIL_DEFAULT;

export function setRailWidth(next: number, persist = true) {
  width = Math.round(Math.min(RAIL_MAX, Math.max(RAIL_MIN, next)));
  if (persist) savePreferences({[WIDTH_KEY]: width});
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

export function setObjectsHeight(next: number, persist = true) {
  objectsHeight = Math.round(Math.min(OBJECTS_MAX, Math.max(OBJECTS_MIN, next)));
  if (persist) savePreferences({[OBJECTS_KEY]: objectsHeight});
  listeners.forEach((l) => l());
}
export const useObjectsHeight = () => useSyncExternalStore(subscribe, () => objectsHeight);

observePreference(KEY, () => { hidden = preferenceText(KEY) === 'hidden'; listeners.forEach(l => l()); });
observePreference(WIDTH_KEY, () => setRailWidth(Number(preferenceText(WIDTH_KEY)) || RAIL_DEFAULT, false));
observePreference(OBJECTS_KEY, () => setObjectsHeight(Number(preferenceText(OBJECTS_KEY)) || OBJECTS_DEFAULT, false));
