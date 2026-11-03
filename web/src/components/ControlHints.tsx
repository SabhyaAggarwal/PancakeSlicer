import { useEffect, useState } from 'react';
import { Tooltip } from 'radix-ui';
const NAV_KEYS = new Set(['Tab', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Home', 'End', 'PageUp', 'PageDown', 'Enter', ' ', 'Escape', 'F6']);
const iconOnly = (node: HTMLElement) => !node.textContent?.trim() && (node.matches('button,a,[role=button],[role=tab],[role=menuitem]') || !!node.querySelector('img,svg'));
export function ControlHints() {
  const [hint, setHint] = useState<{text: string; x: number; y: number} | null>(null);
  useEffect(() => {
    let target: HTMLElement | null = null, timer: ReturnType<typeof setTimeout> | undefined;
    const untitle = (el: Element) => {
      const text = el.getAttribute('title')?.trim();
      el.removeAttribute('title');
      if (text && el instanceof HTMLElement && !el.getAttribute('aria-label') && !el.getAttribute('aria-labelledby') && !el.textContent?.trim()) el.setAttribute('aria-label', text);
    };
    const sweep = (root: Element) => { if (root.hasAttribute('title')) untitle(root); root.querySelectorAll('[title]').forEach(untitle); };
    const titles = new MutationObserver((records) => { for (const r of records) { if (r.type === 'attributes') { if (r.target instanceof Element && r.target.hasAttribute('title')) untitle(r.target); } else r.addedNodes.forEach((n) => { if (n instanceof Element) sweep(n); }); } });
    const root = document.documentElement;
    const pointerInput = () => { root.dataset.input = 'pointer'; };
    const keyInput = (event: KeyboardEvent) => { if (NAV_KEYS.has(event.key) && !event.metaKey && !event.ctrlKey) root.dataset.input = 'keyboard'; };
    document.addEventListener('pointerdown', pointerInput, true); document.addEventListener('keydown', keyInput, true);
    sweep(document.body); titles.observe(document.body, {subtree: true, childList: true, attributes: true, attributeFilter: ['title']});
    const clear = () => { clearTimeout(timer); setHint(null); target = null; };
    const show = (event: PointerEvent) => {
      if (event.pointerType !== 'mouse') return;
      const node = event.target instanceof Element ? event.target.closest<HTMLElement>('button,a,[role=button],[role=tab],[role=menuitem]') : null;
      if (node === target) return;
      clear();
      const text = node && iconOnly(node) ? node.getAttribute('aria-label')?.trim() : undefined;
      if (!node || !text) return;
      target = node;
      timer = setTimeout(() => { if (target !== node || !node.isConnected) return; const r = node.getBoundingClientRect(); setHint({text, x: r.left + r.width / 2, y: r.bottom}); }, 500);
    };
    const leave = (event: PointerEvent) => { if (target?.contains(event.relatedTarget as Node | null)) return; clear(); };
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') clear(); };
    document.addEventListener('pointerover', show); document.addEventListener('pointerout', leave);
    document.addEventListener('pointerdown', clear, true); document.addEventListener('scroll', clear, true); document.addEventListener('keydown', escape);
    return () => { document.removeEventListener('pointerdown', pointerInput, true); document.removeEventListener('keydown', keyInput, true); delete root.dataset.input; titles.disconnect(); clearTimeout(timer); document.removeEventListener('pointerover', show); document.removeEventListener('pointerout', leave); document.removeEventListener('pointerdown', clear, true); document.removeEventListener('scroll', clear, true); document.removeEventListener('keydown', escape); };
  }, []);
  return <Tooltip.Provider><Tooltip.Root open={!!hint}><Tooltip.Trigger asChild><span aria-hidden className="hint-anchor" style={{left: hint?.x ?? 0, top: hint?.y ?? 0}}/></Tooltip.Trigger><Tooltip.Portal><Tooltip.Content aria-hidden className="control-tooltip" side="bottom" sideOffset={6} collisionPadding={12}>{hint?.text}</Tooltip.Content></Tooltip.Portal></Tooltip.Root></Tooltip.Provider>;
}
