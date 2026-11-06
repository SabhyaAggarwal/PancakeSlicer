

import { useLayoutEffect, type RefObject } from 'react';

export function useSubmenuPlace(ref: RefObject<HTMLDivElement | null>) {
  useLayoutEffect(() => {
    const el = ref.current, entry = el?.parentElement;
    if (!el || !entry) return;
    const place = () => {
      const r = entry.getBoundingClientRect(), w = el.offsetWidth, h = el.offsetHeight;
      el.style.left = `${r.right + w <= window.innerWidth - 8 ? r.right : Math.max(8, r.left - w)}px`;
      el.style.top = `${Math.max(8, Math.min(r.top - 5, window.innerHeight - 8 - h))}px`;
    };
    place();
    const menu = entry.closest('.ctx-menu');
    menu?.addEventListener('scroll', place);
    return () => menu?.removeEventListener('scroll', place);
  });
}
