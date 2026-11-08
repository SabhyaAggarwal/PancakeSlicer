import { useEffect, useRef, type ReactNode, type PointerEvent } from 'react';
import { setSettingsSheet, setSettingsSnap, useOverlayRail, usePhoneSheet, useSettingsSnap, type SettingsSnap } from '../workspace-sheet';
import { Glyph } from './Glyph';

const snaps: SettingsSnap[] = ['peek', 'full'];

export default function WorkspaceRail({ children }: { children: ReactNode }) {
  const overlay = useOverlayRail(), phone = usePhoneSheet(), snap = useSettingsSnap();
  const open = overlay && snap !== 'peek', modal = open && (!phone || snap === 'full');
  const panel = useRef<HTMLDivElement>(null), launch = useRef<HTMLButtonElement>(null), handle = useRef<HTMLButtonElement>(null);
  const drag = useRef<{ id: number; y: number; height: number; moved: boolean } | null>(null);
  const suppressClick = useRef(false);

  useEffect(() => { if (!overlay) setSettingsSheet(false); }, [overlay]);
  useEffect(() => {
    const canvas = panel.current?.parentElement?.querySelector<HTMLElement>('.canvas-area');
    if (canvas) canvas.inert = modal;
    return () => { if (canvas) canvas.inert = false; };
  }, [modal]);
  useEffect(() => { if (open) handle.current?.focus({ preventScroll: true }); }, [open]);
  const close = () => {
    setSettingsSheet(false);
    requestAnimationFrame(() => (phone ? handle : launch).current?.focus({ preventScroll: true }));
  };
  const heights = () => {
    const available = panel.current?.parentElement?.clientHeight ?? window.innerHeight;
    return [56, available - 8];
  };
  const start = (event: PointerEvent<HTMLButtonElement>) => {
    if (!phone || event.button !== 0) return;
    suppressClick.current = false;
    drag.current = { id: event.pointerId, y: event.clientY, height: panel.current!.getBoundingClientRect().height, moved: false };
    event.currentTarget.setPointerCapture(event.pointerId);
  };
  const move = (event: PointerEvent<HTMLButtonElement>) => {
    const current = drag.current;
    if (!current || current.id !== event.pointerId) return;
    const delta = current.y - event.clientY;
    if (Math.abs(delta) > 4) current.moved = true;
    if (!current.moved) return;
    const stops = heights();
    const height = Math.max(stops[0], Math.min(stops[stops.length - 1], current.height + delta));
    panel.current!.style.setProperty('--sheet-height', `${height}px`);
    panel.current!.dataset.dragging = 'true';
  };
  const end = (event: PointerEvent<HTMLButtonElement>) => {
    const current = drag.current;
    if (!current || current.id !== event.pointerId) return;
    drag.current = null;
    if (current.moved) {
      const height = panel.current!.getBoundingClientRect().height;
      const stops = heights();
      const nearest = stops.reduce((best, value, i) => Math.abs(value - height) < Math.abs(stops[best] - height) ? i : best, 0);
      setSettingsSnap(snaps[nearest]);
      suppressClick.current = true;
    } else if (event.type === 'pointerup' && event.pointerType === 'touch') {
      setSettingsSnap(snaps[(snaps.indexOf(snap) + 1) % snaps.length]);
      suppressClick.current = true;
    }
    panel.current?.style.removeProperty('--sheet-height');
    if (panel.current) delete panel.current.dataset.dragging;
  };

  return <>
    {overlay && !phone && !open && <button ref={launch} type="button" className="icon-btn canvas-chip workspace-settings-launch" aria-label="Settings" aria-controls="workspace-settings" aria-expanded={false} onClick={() => setSettingsSheet(true)}><Glyph name="rail" /></button>}
    {modal && <div className="workspace-scrim" aria-hidden onClick={close} />}
    <div ref={panel} id="workspace-settings" className="workspace-rail" data-overlay={overlay} data-phone={phone} data-open={open} data-snap={snap}
      role={modal ? 'dialog' : undefined} aria-modal={modal || undefined} aria-label={overlay ? 'Settings' : undefined}
      onKeyDown={event => {
        if (open && event.key === 'Escape' && !event.defaultPrevented && !(event.target as Element).closest('[role=menu],[role=listbox]') && (!modal || (event.target as Element).closest('[role=dialog]') === panel.current)) {
          event.preventDefault(); event.stopPropagation(); close();
        }
        if (modal && event.key === 'Tab' && !event.defaultPrevented) {
          const controls = [...panel.current!.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),select:not(:disabled),[tabindex="0"]')].filter(el => el.getClientRects().length && getComputedStyle(el).visibility !== 'hidden');
          const first = controls[0], last = controls[controls.length - 1];
          if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
          else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
        }
      }}>
      {overlay && <header className="workspace-sheet-heading">
        <button ref={handle} type="button" className="workspace-sheet-handle" aria-label={phone ? `Settings, ${snap} height` : 'Settings'} aria-expanded={open} aria-controls="workspace-settings-body"
          onPointerDown={start} onPointerMove={move} onPointerUp={end} onPointerCancel={end}
          onClick={event => {
            if (event.detail !== 0 && suppressClick.current) { suppressClick.current = false; return; }
            if (phone) setSettingsSnap(snaps[(snaps.indexOf(snap) + 1) % snaps.length]);
          }}
          onKeyDown={event => {
            const direction = ['ArrowUp', 'ArrowRight'].includes(event.key) ? 1 : ['ArrowDown', 'ArrowLeft'].includes(event.key) ? -1 : 0;
            if (phone && direction) { event.preventDefault(); setSettingsSnap(snaps[Math.max(0, Math.min(snaps.length - 1, snaps.indexOf(snap) + direction))]); }
          }}><span className="workspace-sheet-grip" /><strong>Settings</strong><span className="workspace-sheet-caret" /></button>
        {open && <button type="button" className="btn workspace-sheet-done" onClick={close}>Done</button>}
      </header>}
      <div id="workspace-settings-body" className="workspace-settings-body" inert={overlay && !open} aria-hidden={overlay && !open || undefined}>{children}</div>
    </div>
  </>;
}
