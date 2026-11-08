import { useSyncExternalStore } from 'react';

export interface Assembly { on: boolean; ratio: number }
let state: Assembly = { on: false, ratio: 1 };
const listeners = new Set<() => void>();
export const assembly = () => state;
export const subscribeAssembly = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export const useAssembly = () => useSyncExternalStore(subscribeAssembly, assembly);
export function setAssembly(patch: Partial<Assembly>) {
  state = { ...state, ...patch, ...(patch.on === false ? { ratio: 1 } : {}) };
  listeners.forEach((l) => l());
}
