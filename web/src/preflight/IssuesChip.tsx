import { useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { currentResult } from '../flow/slice';
import { speculate } from '../flow/speculate';
import { getState, setState } from '../state/store';
import { dismissIssue, hoverIssue, issuesVersion, plateIssues, subscribeIssues, type Issue } from './issues';
import './preflight.css';

const TITLE: Record<Issue['kind'], string> = {
  bed: 'Off the bed',
  overhang: 'Overhang, no support',
  contact: 'Barely touches the bed',
  footprint: 'Small footprint',
};

const select = (id: number) => {
  const inst = getState().objects.find((o) => o.id === id)?.instances[0];
  if (inst) setState({ tab: 'prepare', selected: { object: id, instance: inst.id } });
};

function cost(base: ReturnType<typeof currentResult>, next: Awaited<ReturnType<typeof speculate>>) {
  const a = base?.summary;
  if (!a || !next) return '';
  const parts: string[] = [];
  if (a.timeS != null && next.timeS != null && Math.abs(next.timeS - a.timeS) >= 60) {
    const m = Math.round((next.timeS - a.timeS) / 60);
    parts.push(`${m > 0 ? '+' : '−'}${Math.abs(m)} min`);
  }
  if (a.grams != null && next.grams != null && Math.abs(next.grams - a.grams) >= 0.1) {
    const g = Math.round((next.grams - a.grams) * 10) / 10;
    parts.push(`${g > 0 ? '+' : '−'}${Math.abs(g)} g`);
  }
  return parts.length ? parts.join(' · ') : '±0';
}

export default function IssuesChip() {
  useSyncExternalStore(subscribeIssues, issuesVersion);
  const issues = plateIssues();
  const [open, setOpen] = useState(false);
  const [at, setAt] = useState({ top: 0, right: 0 });
  const [costs, setCosts] = useState<Record<string, string>>({});
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const away = (e: PointerEvent) => !ref.current?.contains(e.target as Node) && setOpen(false);
    window.addEventListener('pointerdown', away);
    return () => window.removeEventListener('pointerdown', away);
  }, [open]);
  const keys = issues.map((i) => i.key).join();
  useEffect(() => {
    if (!open) return;
    const ctl = new AbortController();
    void (async () => {
      for (const i of plateIssues()) {
        if (!i.fix.edit || ctl.signal.aborted) continue;
        const next = await speculate(i.fix.edit, { signal: ctl.signal });
        if (!ctl.signal.aborted) setCosts((c) => ({ ...c, [i.key]: cost(currentResult(), next) }));
      }
    })();
    return () => ctl.abort();
  }, [open, keys]);
  useEffect(() => { if (!issues.length) setOpen(false); }, [issues.length]);
  if (!issues.length) return null;
  return (
    <div className="issues" ref={ref} onKeyDown={(e) => { if (e.key === 'Escape') { setOpen(false); e.stopPropagation(); } }}>
      <button type="button" className="issues-chip" aria-expanded={open} aria-haspopup="true" onClick={(e) => {
        const r = e.currentTarget.getBoundingClientRect();
        setAt({ top: r.bottom + 6, right: window.innerWidth - r.right });
        setOpen(!open);
      }}>
        Issues {issues.length}
      </button>
      {open && (
        <div className="issues-pop" role="list" style={at} onPointerLeave={() => hoverIssue(null)}>
          {issues.map((i) => (
            <div key={i.key} className="issue-row" role="listitem" onPointerEnter={() => hoverIssue(i.key)} onFocus={() => hoverIssue(i.key)}>
              <button type="button" className="issue-what" onClick={() => select(i.objectId)}>
                <span className="issue-title">{TITLE[i.kind]}</span>
                <span className="issue-name">{i.name.replace(/\.(stl|obj|3mf|step|stp)$/i, '')}</span>
              </button>
              <span className="issue-cost num">{i.fix.edit ? costs[i.key] ?? '…' : ''}</span>
              <button type="button" className="btn-ghost issue-fix" onClick={() => { hoverIssue(null); i.fix.run(); }}>{i.fix.label}</button>
              <button type="button" className="icon-btn issue-dismiss" aria-label="Dismiss" onClick={() => dismissIssue(i.key)}>×</button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
