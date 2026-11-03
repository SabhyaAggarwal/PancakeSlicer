import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react';
import { createPortal } from 'react-dom';
import { Popover } from 'radix-ui';
import { backToLatest, changeLine, closeHistory, dayOf, exactTime, historyState, restoreVersion, rowTimes, useHistory, versionSetup, versionTime,
  viewVersion, type ProjectVersion, type VersionSetup } from './versions';

const short = (s: number) => { const m = Math.round(s / 60); return m >= 60 ? `${Math.floor(m / 60)}h ${m % 60}m` : `${m}m`; };
const long = (s: number) => { const t = Math.round(s), h = Math.floor(t / 3600), m = Math.floor(t / 60) % 60; return h ? `${h}h ${m}m ${t % 60}s` : `${m}m ${t % 60}s`; };

const changes = (v: ProjectVersion, first: boolean) => v.change.length ? v.change.map(changeLine) : [first ? 'Created' : 'Edited'];
const sliced = <svg className="vh-sliced" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M8 2 14 5 8 8 2 5zM2 8l6 3 6-3M2 11l6 3 6-3" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinejoin="round"/></svg>;
const info = <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="6.25" fill="none" stroke="currentColor" strokeWidth="1.3"/><path d="M8 7.2v4M8 4.9v.1" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round"/></svg>;

export default function VersionHistory() {
  const h = useHistory();
  const [host, setHost] = useState<Element | null>(null);
  useEffect(() => { if (h.open) setHost(document.querySelector('.canvas-area')); }, [h.open]);
  return h.open && host ? createPortal(<Panel />, host) : null;
}

function Card({ projectId, v, first, current, close }: { projectId: string; v: ProjectVersion; first: boolean; current: boolean; close: () => void }) {
  const [setup, setSetup] = useState<VersionSetup | null>(null);
  useEffect(() => { let live = true; versionSetup(projectId, v.id).then((s) => live && setSetup(s), () => {}); return () => { live = false; }; }, [projectId, v.id]);
  const s = v.slice;
  const stats = s ? [s.timeS != null && long(s.timeS), s.grams != null && `${s.grams.toFixed(1)} g`, s.mm != null && `${(s.mm / 1000).toFixed(2)} m`,
    s.layers != null && `${s.layers} layers`, s.supportG ? `support ${s.supportG.toFixed(1)} g` : false].filter(Boolean).join(' · ') : 'Not sliced';
  const restore = async () => {
    close();
    if (historyState().viewing?.id !== v.id) await viewVersion(v);
    if (historyState().viewing?.id === v.id) await restoreVersion();
  };
  return <>
    {setup?.picture ? <img className="vh-card-plate" src={setup.picture} alt="" /> : <div className="vh-card-plate" />}
    <ul className="vh-changes">{changes(v, first).map((c, i) => <li key={i}>{c}</li>)}</ul>
    {setup && <p>{[setup.printer, setup.filaments.filter(Boolean).join(', '), setup.process].filter(Boolean).join(' · ')}</p>}
    <p>{stats}</p>
    <p className="vh-meta"><time dateTime={new Date(v.until).toISOString()}>{exactTime(v.until)}</time> · Version {v.id}</p>
    {!current && <button type="button" className="btn btn-primary vh-card-restore" onClick={() => void restore()}>Restore</button>}
  </>;
}

function Panel() {
  const h = useHistory(), list = h.versions;
  const shown = h.loading ?? h.viewing?.id ?? list?.[0]?.id;
  const [card, setCard] = useState<{ id: number; pinned: boolean } | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout>>(undefined);
  const later = (next: typeof card, ms: number) => { clearTimeout(timer.current); timer.current = setTimeout(() => setCard(next), ms); };
  const hover = (id: number | null) => (e: PointerEvent) => {
    if (e.pointerType !== 'mouse' || card?.pinned) return;
    if (id === null) later(null, 150); else if (card?.id === id) clearTimeout(timer.current); else later({ id, pinned: false }, card ? 0 : 250);
  };
  useEffect(() => () => clearTimeout(timer.current), []);
  const narrow = window.innerWidth < 600;
  const body = useRef<HTMLUListElement>(null);
  const [now, setNow] = useState(Date.now);
  useEffect(() => { const timer = setInterval(() => setNow(Date.now()), 30_000); return () => clearInterval(timer); }, []);
  const times = useMemo(() => rowTimes(list?.slice(1) ?? [], now), [list, now]);
  const days = useMemo(() => {
    const out: [string, ProjectVersion[]][] = [];
    for (const v of list ?? []) {
      const day = dayOf(v.until), last = out[out.length - 1];
      if (last?.[0] === day) last[1].push(v); else out.push([day, [v]]);
    }
    return out;
  }, [list]);
  useEffect(() => { body.current?.querySelector<HTMLElement>('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest' }); }, [shown]);
  const step = (to: number) => {
    if (!list?.length) return;
    const i = Math.max(0, list.findIndex((v) => v.id === shown));
    const next = list[Math.min(list.length - 1, Math.max(0, to === Infinity ? list.length - 1 : to === -Infinity ? 0 : i + to))];
    if (next && next.id !== shown) {
      void viewVersion(next);
      requestAnimationFrame(() => body.current?.querySelector<HTMLElement>(`[data-version="${next.id}"]`)?.focus());
    }
  };
  const keys = (e: KeyboardEvent) => {
    const move = { ArrowDown: 1, ArrowUp: -1, End: Infinity, Home: -Infinity }[e.key];
    const inList = !!(e.target as Element).closest('.vh-list');
    if (move !== undefined) { e.preventDefault(); e.stopPropagation(); step(move); }
    else if ((e.key === 'ArrowRight' || e.key === 'Enter') && inList && shown !== undefined) {
      e.preventDefault(); e.stopPropagation(); setCard(e.key === 'Enter' && card?.id === shown ? null : { id: shown, pinned: true });
    } else if (e.key === 'ArrowLeft' && inList) { e.preventDefault(); e.stopPropagation(); setCard(null); }
    else if (e.key === 'Escape') { e.stopPropagation(); if (card) setCard(null); else if (h.viewing) void backToLatest(); else closeHistory(); }
  };
  const last = list?.[list.length - 1]?.id;
  return (
    <section className="vh-panel" aria-label="Version history" onKeyDown={keys}>
      <div className="outliner-heading">
        <h2 className="vh-title">History</h2>
        <span id="vh-about" hidden>Details of</span>
        <button type="button" className="btn-ghost vh-close" aria-label="Close history" onClick={closeHistory}><svg width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M3.5 3.5l9 9M12.5 3.5l-9 9" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/></svg></button>
      </div>
      {h.error ? <p className="vh-empty" role="alert">{h.error}</p>
        : !list ? <p className="vh-empty" role="status">Loading…</p>
        : !list.length ? <p className="vh-empty">No versions yet</p>
        : <ul className="vh-list scroll-fade" ref={body} role="listbox" aria-label="Versions">
          {days.map(([day, rows]) => <li key={day} role="presentation"><span className="vh-day-name" aria-hidden="true">{day}</span><ul role="group" aria-label={day}>
            {rows.map((v) => {
              const current = v.id === list[0].id, open = card?.id === v.id, time = current ? 'Current' : times.get(v.id);
              const print = v.slice?.timeS != null ? short(v.slice.timeS) : v.slice ? 'Sliced' : '';
              return <Popover.Root key={v.id} open={open} onOpenChange={(next) => !next && setCard(null)}><Popover.Anchor asChild><li role="presentation" className="vh-item">
                <button type="button" role="option" className="vh-row" data-version={v.id} data-current={current || undefined} aria-selected={v.id === shown}
                  aria-busy={h.loading === v.id || undefined} aria-label={print ? `${time}. Sliced, ${print}` : time} onClick={() => void viewVersion(v)}>
                  <time className="vh-time" id={`vh-t-${v.id}`} dateTime={new Date(v.until).toISOString()}>{time}</time>
                  {print && <span className="vh-print">{sliced}{print}</span>}
                </button>
                <Popover.Trigger asChild>
                  <button type="button" className="btn-ghost vh-info" aria-labelledby={`vh-about vh-t-${v.id}`} onPointerEnter={hover(v.id)} onPointerLeave={hover(null)}
                    onFocus={(e) => e.currentTarget.matches(':focus-visible') && !card?.pinned && setCard({ id: v.id, pinned: false })}
                    onBlur={() => !card?.pinned && setCard(null)}
                    onClick={(e) => { e.preventDefault(); clearTimeout(timer.current); setCard(open && card?.pinned ? null : { id: v.id, pinned: true }); }}>{info}</button>
                </Popover.Trigger>
                </li></Popover.Anchor>
                <Popover.Portal><Popover.Content className="vh-card" side={narrow ? 'bottom' : 'left'} align="start" sideOffset={narrow ? 4 : 16} collisionPadding={8}
                  onOpenAutoFocus={(e) => e.preventDefault()} onCloseAutoFocus={(e) => e.preventDefault()} onPointerEnter={hover(v.id)} onPointerLeave={hover(null)}>
                  {h.projectId && <Card projectId={h.projectId} v={v} first={v.id === last} current={current} close={() => setCard(null)} />}
                </Popover.Content></Popover.Portal>
              </Popover.Root>;
            })}
          </ul></li>)}
        </ul>}
      {h.viewing && <div className="vh-viewing" role="status">
        <span className="vh-viewing-name">Viewing {versionTime(h.viewing.until)}</span>
        <button type="button" className="btn btn-primary" onClick={() => void restoreVersion()}>Restore</button>
        <button type="button" className="btn" onClick={() => void backToLatest()}>Latest</button>
      </div>}
    </section>
  );
}
