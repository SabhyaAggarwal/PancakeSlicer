import { useEffect, useMemo, useRef, useState, type KeyboardEvent } from 'react';
import { createPortal } from 'react-dom';
import { backToLatest, changeLine, closeHistory, dayOf, exactTime, restoreVersion, rowTimes, useHistory, versionSetup, versionTime, viewVersion,
  type ProjectVersion, type VersionSetup } from './versions';

const short = (s: number) => { const m = Math.round(s / 60); return m >= 60 ? `${Math.floor(m / 60)}h ${m % 60}m` : `${m}m`; };
const long = (s: number) => { const t = Math.round(s), h = Math.floor(t / 3600), m = Math.floor(t / 60) % 60; return h ? `${h}h ${m}m ${t % 60}s` : `${m}m ${t % 60}s`; };

function summary(v: ProjectVersion, first: boolean) {
  const parts = v.change.map(changeLine);
  return parts.length ? parts.join(', ') : first ? 'Created' : 'Edited';
}
function sliceShort(v: ProjectVersion) {
  const s = v.slice;
  return s ? [s.timeS != null && short(s.timeS), s.grams != null && `${s.grams.toFixed(1)} g`].filter(Boolean).join(' · ') : '';
}

export default function VersionHistory() {
  const h = useHistory();
  const [host, setHost] = useState<Element | null>(null);
  useEffect(() => { if (h.open) setHost(document.querySelector('.canvas-area')); }, [h.open]);
  return h.open && host ? createPortal(<Panel />, host) : null;
}

function Details({ projectId, v, first }: { projectId: string; v: ProjectVersion; first: boolean }) {
  const [setup, setSetup] = useState<VersionSetup | null>(null);
  useEffect(() => { let live = true; versionSetup(projectId, v.id).then((s) => live && setSetup(s), () => {}); return () => { live = false; }; }, [projectId, v.id]);
  const s = v.slice;
  const stats = s && [s.timeS != null && long(s.timeS), s.grams != null && `${s.grams.toFixed(1)} g`, s.mm != null && `${(s.mm / 1000).toFixed(2)} m`,
    s.layers != null && `${s.layers} layers`, s.supportG ? `support ${s.supportG.toFixed(1)} g` : false].filter(Boolean).join(' · ');
  return (
    <div className="vh-detail" id={`vh-detail-${v.id}`}>
      <ul className="vh-changes">{v.change.length ? v.change.map((c, i) => <li key={i}>{changeLine(c)}</li>) : <li>{first ? 'Created' : 'Edited'}</li>}</ul>
      {setup && <p className="vh-setup">{[setup.printer, setup.filaments.filter(Boolean).join(', '), setup.process].filter(Boolean).join(' · ')}</p>}
      {stats && <p className="vh-setup">{stats}</p>}
      <p className="vh-meta"><time dateTime={new Date(v.until).toISOString()}>{exactTime(v.until)}</time> · Version {v.id}</p>
    </div>
  );
}

function Panel() {
  const h = useHistory(), list = h.versions;
  const shown = h.loading ?? h.viewing?.id ?? list?.[0]?.id;
  const [expanded, setExpanded] = useState<number | null>(null);
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
  useEffect(() => { body.current?.querySelector<HTMLElement>('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest' }); }, [shown, expanded]);
  const choose = (v: ProjectVersion) => {
    if (v.id === shown) setExpanded(expanded === v.id ? null : v.id);
    else void viewVersion(v);
  };
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
      e.preventDefault(); e.stopPropagation(); setExpanded(e.key === 'Enter' && expanded === shown ? null : shown);
    } else if (e.key === 'ArrowLeft' && inList) { e.preventDefault(); e.stopPropagation(); setExpanded(null); }
    else if (e.key === 'Escape') { e.stopPropagation(); if (expanded !== null) setExpanded(null); else if (h.viewing) void backToLatest(); else closeHistory(); }
  };
  const last = list?.[list.length - 1]?.id;
  return (
    <section className="vh-panel" data-expanded={expanded !== null || undefined} aria-label="Version history" onKeyDown={keys}>
      <div className="outliner-heading">
        <h2 className="vh-title">History</h2>
        <button type="button" className="btn-ghost vh-close" aria-label="Close history" onClick={closeHistory}>×</button>
      </div>
      {h.error ? <p className="vh-empty" role="alert">{h.error}</p>
        : !list ? <p className="vh-empty" role="status">Loading…</p>
        : !list.length ? <p className="vh-empty">No versions yet</p>
        : <ul className="vh-list scroll-fade" ref={body} role="listbox" aria-label="Versions">
          {days.map(([day, rows]) => <li key={day} role="presentation"><span className="vh-day-name" aria-hidden="true">{day}</span><ul role="group" aria-label={day}>
            {rows.map((v) => {
              const current = v.id === list[0].id, open = expanded === v.id, stats = sliceShort(v), what = summary(v, v.id === last);
              return <li key={v.id} role="presentation">
                <button type="button" role="option" className="vh-row" data-version={v.id} data-current={current || undefined} aria-selected={v.id === shown}
                  aria-expanded={open} aria-controls={open ? `vh-detail-${v.id}` : undefined} aria-busy={h.loading === v.id || undefined}
                  aria-label={`${current ? 'Current' : times.get(v.id)}: ${what}${stats ? `. Sliced ${stats}` : ''}`} onClick={() => choose(v)}>
                  <time className="vh-time" dateTime={new Date(v.until).toISOString()}>{current ? 'Current' : times.get(v.id)}</time>
                  <span className="vh-what">{what}</span>
                  {stats ? <span className="vh-stats"><svg className="vh-sliced" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M8 2 14 5 8 8 2 5zM2 8l6 3 6-3M2 11l6 3 6-3" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinejoin="round"/></svg><span className="vh-stats-text">{stats}</span></span> : <span />}
                  <svg className="vh-chevron" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M6 3.5 10.5 8 6 12.5" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/></svg>
                </button>
                {open && h.projectId && <Details projectId={h.projectId} v={v} first={v.id === last} />}
              </li>;
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
