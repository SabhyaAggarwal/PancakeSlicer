import { useLayoutEffect, useState, type RefObject } from 'react';
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

export function useTabFit(ref: RefObject<HTMLElement | null>, key: unknown): number[] {
  const [breaks, setBreaks] = useState<number[]>([]);
  useLayoutEffect(() => {
    const strip = ref.current;
    if (!strip) return;
    const need = { icons: 0, text: 0 }, SLACK = 4;
    let fit = strip.getAttribute('data-fit') ?? '', widths: number[] = [], planned = '';
    const tabs = () => [...strip.querySelectorAll<HTMLElement>(':scope > .page-tab')];
    const width = (mode: string) => { if (mode) strip.setAttribute('data-fit', mode); else strip.removeAttribute('data-fit'); return strip.getBoundingClientRect().width; };
    const measure = () => {
      strip.setAttribute('data-measure', '');
      need.icons = width(''); need.text = width('text');
      widths = tabs().map((t) => { const cs = getComputedStyle(t); return t.getBoundingClientRect().width + parseFloat(cs.marginLeft) + parseFloat(cs.marginRight); });
      strip.removeAttribute('data-measure');
      width(fit);
    };
    const plan = (room: number): number[][] => {
      const rows: number[][] = [];
      for (let i = 0, sum = 0; i < widths.length; i++) { if (!rows.length || sum + widths[i] > room) { rows.push([]); sum = 0; } rows[rows.length - 1].push(i); sum += widths[i]; }
      const last = rows[rows.length - 1], prev = rows[rows.length - 2];
      if (prev && last.length === 1 && prev.length > 2) last.unshift(prev.pop()!);
      return rows;
    };
    const apply = () => {
      const w = strip.clientWidth;
      const next = w >= need.icons + (fit ? SLACK : 0) ? '' : w >= need.text + (fit === 'wrap' ? SLACK : 0) ? 'text' : 'wrap';
      if (next !== fit) { fit = next; width(fit); }
      const cs = getComputedStyle(strip), rows = fit === 'wrap' ? plan(w - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight) - 1) : [];
      tabs().forEach((t, i) => { const row = rows.find((r) => r.includes(i)); t.toggleAttribute('data-row-fill', !!row && row.length >= 2); t.style.setProperty('--tab-grow', String(Math.round(widths[i] ?? 1))); });
      const next2 = rows.slice(1).map((r) => r[0]), sig = next2.join(',');
      if (sig !== planned) { planned = sig; setBreaks(next2); }
    };
    measure(); apply();
    let live = true;
    void document.fonts?.ready.then(() => { if (live) { measure(); apply(); } });
    const observer = new ResizeObserver(apply);
    observer.observe(strip);
    return () => { live = false; observer.disconnect(); };
  }, [ref, key]);
  return breaks;
}
