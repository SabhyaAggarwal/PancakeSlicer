import { useSyncExternalStore } from 'react';
import { narrowQuery } from './breakpoints';
import { sidebarHidden, toggleSidebar } from './sidebar';
import { resolvePreset, type State } from './state/store';

export const printerReady = (s: State) => { const r = s.printer ? resolvePreset(s, 'machine', s.printer) : undefined; return !!r?.base && !r.error; };

export { narrowQuery, useNarrow } from './breakpoints';
const listeners = new Set<() => void>();
let open = false;

const subscribe = (l: () => void) => { listeners.add(l); return () => void listeners.delete(l); };

export function setSettingsSheet(next: boolean) {
  if (open === next) return;
  open = next;
  listeners.forEach((l) => l());
}

export function showSettings() {
  if (narrowQuery.matches) setSettingsSheet(true);
  else if (sidebarHidden()) toggleSidebar(false);
}

export const useSettingsSheet = () => useSyncExternalStore(subscribe, () => open);
