import { useEffect, useMemo, useRef, useState, type KeyboardEvent } from 'react';
import { createPortal } from 'react-dom';
import { changeText } from './version-change';
import { backToLatest, closeHistory, dayOf, restoreVersion, useHistory, versionClock, versionTime, viewVersion, type ProjectVersion } from './versions';

const hours = (s: number) => { const m = Math.round(s / 60); return m >= 60 ? `${Math.floor(m / 60)} h ${m % 60} min` : `${m} min`; };

function summary(v: ProjectVersion, first: boolean) {
  const parts = v.change.map(changeText);
  if (!parts.length) return first ? 'Created' : 'Edited';
  return parts.length > 2 ? `${parts.slice(0, 2).join(', ')} +${parts.length - 2}` : parts.join(', ');
}

export default function VersionHistory() {
  const h = useHistory();
  const [host, setHost] = useState<Element | null>(null);
  useEffect(() => { if (h.open) setHost(document.querySelector('.canvas-area')); }, [h.open]);
  return h.open && host ? createPortal(<Panel />, host) : null;
}

function Panel() {
  const h = useHistory(), list = h.versions;
  const shown = h.loading ?? h.viewing?.id ?? list?.[0]?.id;
  const body = useRef<HTMLUListElement>(null);
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
    if (move !== undefined) { e.preventDefault(); e.stopPropagation(); step(move); }
    else if (e.key === 'Escape') { e.stopPropagation(); if (h.viewing) void backToLatest(); else closeHistory(); }
  };
  const last = list?.[list.length - 1]?.id;
  return (
    <section className="vh-panel" aria-label="Version history" onKeyDown={keys}>
      <div className="outliner-heading">
        <h2 className="vh-title">History</h2>
        <button type="button" className="btn-ghost vh-close" aria-label="Close history" onClick={closeHistory}>×</button>
      </div>
      {h.error ? <p className="vh-empty" role="alert">{h.error}</p>
        : !list ? <p className="vh-empty" role="status">Loading…</p>
        : !list.length ? <p className="vh-empty">No versions yet</p>
        : <ul className="vh-list scroll-fade" ref={body} role="listbox" aria-label="Versions">
          {days.map(([day, rows]) => <li key={day} role="presentation"><span className="vh-day-name" aria-hidden="true">{day}</span><ul role="group" aria-label={day}>
            {rows.map((v) => <li key={v.id} role="presentation">
              <button type="button" role="option" className="vh-row" data-version={v.id} aria-selected={v.id === shown}
                aria-busy={h.loading === v.id || undefined} onClick={() => void viewVersion(v)}>
                <span className="vh-line">
                  <time dateTime={new Date(v.until).toISOString()}>{versionClock(v.until)}</time>
                  {v.id === list[0].id && <span className="vh-tag">Current</span>}
                  {v.slice && <span className="vh-tag vh-sliced">Sliced</span>}
                </span>
                <span className="vh-what">{summary(v, v.id === last)}</span>
                {v.slice && (v.slice.timeS != null || v.slice.grams != null) && <span className="vh-stats">
                  {[v.slice.timeS != null && hours(v.slice.timeS), v.slice.grams != null && `${v.slice.grams.toFixed(1)} g`].filter(Boolean).join(' · ')}
                </span>}
              </button>
            </li>)}
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
