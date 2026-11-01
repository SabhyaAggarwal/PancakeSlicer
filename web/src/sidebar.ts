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
