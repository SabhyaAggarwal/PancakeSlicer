import { useCallback, useRef, useState, useSyncExternalStore } from 'react';

export type Tier = 'wide' | 'medium' | 'narrow' | 'phone';
export const BP = { medium: 900, wide: 1200, narrow: 600 } as const;

const below = (px: number) => window.matchMedia(`(max-width: ${px - 0.02}px)`);
const queries = { medium: below(BP.wide), narrow: below(BP.medium), phone: below(BP.narrow) };

export function tier(): Tier {
  return queries.phone.matches ? 'phone' : queries.narrow.matches ? 'narrow' : queries.medium.matches ? 'medium' : 'wide';
}
export const narrowQuery = queries.narrow;
export const phoneQuery = queries.phone;

const subscribe = (l: () => void) => {
  const all = Object.values(queries);
  all.forEach((q) => q.addEventListener('change', l));
  return () => all.forEach((q) => q.removeEventListener('change', l));
};
export const useTier = () => useSyncExternalStore(subscribe, tier);
export const useNarrow = () => useSyncExternalStore(subscribe, () => narrowQuery.matches);

export function tierOf(width: number): Tier {
  return width < BP.narrow ? 'phone' : width < BP.medium ? 'narrow' : width < BP.wide ? 'medium' : 'wide';
}

export function useContainerTier(): [Tier, (el: HTMLElement | null) => void] {
  const [t, setT] = useState<Tier>(tier);
  const observer = useRef<ResizeObserver | null>(null);
  const ref = useCallback((el: HTMLElement | null) => {
    observer.current?.disconnect();
    observer.current = null;
    if (!el) return;
    const fit = () => setT(tierOf(el.getBoundingClientRect().width));
    fit();
    observer.current = new ResizeObserver(fit);
    observer.current.observe(el);
  }, []);
  return [t, ref];
}
