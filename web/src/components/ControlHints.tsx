import { useEffect, useState } from 'react';
import { Tooltip } from 'radix-ui';
export function ControlHints() {
  const [hint, setHint] = useState<{text: string; x: number; y: number} | null>(null);
  useEffect(() => {
    let target: HTMLElement | null = null, timer: ReturnType<typeof setTimeout> | undefined, previous: string | null = null;
    const clear = () => { clearTimeout(timer); setHint(null); if (target) { if (previous) target.setAttribute('aria-describedby', previous); else target.removeAttribute('aria-describedby'); } target = null; };
    const show = (event: Event) => {
      const node = event.target instanceof Element ? event.target.closest<HTMLElement>('[title],[data-control-hint]') : null;
      if (!node || node.closest('[data-control-tooltip]')) { clear(); return; }
      if (node === target) return;
      clear(); target = node;
      const text = node.getAttribute('title') ?? node.dataset.controlHint ?? '';
      if (!text.trim()) return;
      node.dataset.controlHint = text; node.removeAttribute('title');
      const disabled = node.closest<HTMLElement>('[disabled],[aria-disabled=true],[data-disabled]');
      const ancestorReason = disabled?.getAttribute('title') ?? disabled?.dataset.controlHint ?? disabled?.getAttribute('aria-description');
      previous = node.getAttribute('aria-describedby');
      timer = setTimeout(() => { if (target !== node || !node.isConnected) return; const r = node.getBoundingClientRect(); node.setAttribute('aria-describedby', [previous, 'orca-control-hint'].filter(Boolean).join(' ')); setHint({text: ancestorReason && !text.includes(ancestorReason) ? `${text}\n${ancestorReason}` : text, x: r.left + r.width / 2, y: r.bottom}); }, event.type === 'focusin' ? 0 : 350);
    };
    const leave = (event: Event) => { if (event instanceof MouseEvent && target?.contains(event.relatedTarget as Node | null)) return; clear(); };
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') clear(); };
    document.addEventListener('pointerover', show); document.addEventListener('focusin', show);
    document.addEventListener('pointerout', leave); document.addEventListener('focusout', leave);
    document.addEventListener('scroll', clear, true); document.addEventListener('keydown', escape);
    return () => { clearTimeout(timer); document.removeEventListener('pointerover', show); document.removeEventListener('focusin', show); document.removeEventListener('pointerout', leave); document.removeEventListener('focusout', leave); document.removeEventListener('scroll', clear, true); document.removeEventListener('keydown', escape); };
  }, []);
  return <Tooltip.Provider><Tooltip.Root open={!!hint}><Tooltip.Trigger asChild><span aria-hidden className="hint-anchor" style={{left: hint?.x ?? 0, top: hint?.y ?? 0}}/></Tooltip.Trigger><Tooltip.Portal><Tooltip.Content id="orca-control-hint" data-control-tooltip className="control-tooltip" side="bottom" sideOffset={6} collisionPadding={12}>{hint?.text}</Tooltip.Content></Tooltip.Portal></Tooltip.Root></Tooltip.Provider>;
}
