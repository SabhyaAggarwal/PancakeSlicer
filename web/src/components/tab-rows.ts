import { useLayoutEffect, type RefObject } from 'react';
export function useRowFill(ref: RefObject<HTMLElement | null>, key: unknown) {
  useLayoutEffect(() => {
    const strip = ref.current;
    if (!strip) return;
    const mark = () => {
      const items = [...strip.children] as HTMLElement[], count = new Map<number, number>();
      for (const item of items) count.set(item.offsetTop, (count.get(item.offsetTop) ?? 0) + 1);
      for (const item of items) item.toggleAttribute('data-row-fill', (count.get(item.offsetTop) ?? 0) >= 3);
    };
    mark();
    const observer = new ResizeObserver(mark);
    observer.observe(strip);
    return () => observer.disconnect();
  }, [ref, key]);
}
