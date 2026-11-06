

import type { PreviewData } from './data';

export interface VisibleToolpaths { data: PreviewData; layers: [number, number] }
let visible: VisibleToolpaths | null = null;
const listeners = new Set<() => void>();
export const visibleToolpaths = () => visible;
export const subscribeVisibleToolpaths = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export function setVisibleToolpaths(v: VisibleToolpaths | null) {
  visible = v;
  listeners.forEach((l) => l());
}
