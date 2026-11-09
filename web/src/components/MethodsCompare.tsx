import { cancelMethods, closeMethods, keepMethod, useMethods, viewMethod, type MethodRun } from '../methods';
import { curvedTopPercent, objectResults, readRecord } from '../preview/explain-record';
import { shortTime } from '../preview/format';
import { useStore } from '../state/store';
import './methods-compare.css';

function facts(run: MethodRun): { main?: string; detail: string; warn?: boolean } {
  if (run.state === 'waiting') return { detail: 'Waiting' };
  if (run.state === 'running') return { detail: run.queued ? 'Queued' : `Slicing ${Math.round(run.pct)}%` };
  if (run.state === 'canceled') return { detail: 'Canceled' };
  if (run.state === 'failed') return { detail: run.error, warn: true };
  const record = run.job.status?.record;
  const s = readRecord(record);
  const main = s ? [s.timeS != null ? shortTime(s.timeS) : null, s.grams != null ? `${s.grams.toFixed(1)} g` : null].filter(Boolean).join(' · ') : undefined;
  if (!s) return { main, detail: '' };
  const hits = s.verifier?.ran ? s.verifier.hits : null;
  const head = hits != null ? `${hits} ${hits === 1 ? 'hit' : 'hits'}` : null;
  const error = s.topErrorMm != null ? `${Math.round(s.topErrorMm * 1000)} µm` : null;
  if (run.method !== 'off' && !s.curved) {
    const why = objectResults(record).find((o) => o.printedFlat)?.flatWhy ?? s.refusedWhy;
    return { main, detail: ['Flat', why].filter(Boolean).join(': '), warn: true };
  }
  const tilt = objectResults(record).find((o) => o.slant)?.slant?.angle, pct = curvedTopPercent(record);
  const top = run.method === 'off' ? null : tilt != null ? `Tilt ${Math.round(tilt)}°` : s.wholePart ? 'Full-part curved layers' : pct != null ? `${pct}% of top curved` : null;
  return { main, detail: [top, error, head].filter(Boolean).join(' · '), warn: !!hits };
}

export function DoneMethods() {
  return <button type="button" className="btn sd-done" onClick={closeMethods}>Done</button>;
}

export function MethodsCompare() {
  const session = useMethods();
  const shown = useStore((s) => s.job.id), stale = useStore((s) => !!s.job.stale);
  if (!session) return null;
  const pending = session.runs.some((r) => r.state === 'waiting' || r.state === 'running');
  const current = session.runs.find((r) => r.state === 'done' && r.job.id === shown);
  return (
    <section className="lg-section methods-compare" aria-label="All methods">
      <div className="lg-section-title">All methods</div>
      <ul className="mc-rows">
        {session.runs.map((run) => {
          const f = facts(run);
          return (
            <li key={run.method}>
              <button type="button" className="mc-row" aria-current={run === current || undefined} disabled={run.state !== 'done'}
                onClick={() => viewMethod(run.method)} title={f.detail || undefined}>
                <span className="mc-name">{run.label}</span><span className="mc-main">{f.main}</span>
                {f.detail && <span className="mc-detail" data-warn={f.warn || undefined}>{f.detail}</span>}
              </button>
            </li>
          );
        })}
      </ul>
      <div className="mc-actions">
        {current && stale && <button type="button" className="btn-primary mc-keep" onClick={() => keepMethod(current.method)}>Keep {current.label}</button>}
        {pending && <button type="button" className="lg-mode" onClick={cancelMethods}>Cancel</button>}
      </div>
    </section>
  );
}
