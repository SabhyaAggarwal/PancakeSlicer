let placing = false;
const listeners = new Set<() => void>();
export const textPlacing = () => placing;
export const subscribeTextPlacing = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export function setTextPlacing(on: boolean) {
  placing = on;
  listeners.forEach((l) => l());
}
