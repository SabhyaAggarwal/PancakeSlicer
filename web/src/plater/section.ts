


export interface SectionView { open: boolean; ratio: number; normal: [number, number, number] | null; align: number }
let state: SectionView = { open: false, ratio: 0, normal: null, align: 0 };
const listeners = new Set<() => void>();
export const sectionView = () => state;
export const subscribeSection = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));
export function setSection(patch: Partial<SectionView>) {
  state = { ...state, ...patch, ratio: Math.min(1, Math.max(0, patch.ratio ?? state.ratio)) };
  listeners.forEach((l) => l());
}

export const alignSection = () => setSection({ align: state.align + 1 });
