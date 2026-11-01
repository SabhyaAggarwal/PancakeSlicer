import { useSyncExternalStore } from 'react';

export interface HoverLink { object: number; instance?: number; source: 'list' | 'canvas' }
let link: HoverLink | null = null;
const listeners = new Set<() => void>();
export const hoverLink = () => link;
export const subscribeHoverLink = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export const useHoverLink = () => useSyncExternalStore(subscribeHoverLink, hoverLink);
export function setHoverLink(next: HoverLink | null) {
  if (next === link || (next && link && next.object === link.object && next.instance === link.instance && next.source === link.source)) return;
  link = next;
  listeners.forEach((l) => l());
}
