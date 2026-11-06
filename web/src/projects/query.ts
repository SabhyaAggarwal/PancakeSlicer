
import { useSyncExternalStore } from 'react';

let text = '';
const listeners = new Set<() => void>();

export const setProjectsQuery = (value: string) => {
  text = value;
  listeners.forEach((l) => l());
};
export const useProjectsQuery = () => useSyncExternalStore((l) => (listeners.add(l), () => void listeners.delete(l)), () => text);
