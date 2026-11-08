import { useStore } from '../state/store';
import { openDevice } from './sendOutcome';
import { statusLine } from './connections';
import './printers.css';

export const shortName = (model: string) => {
  const words = model.replace(/^bambu\s*lab\s*/i, '').split(/\s+/).filter(Boolean);
  return (words.find((w) => /\d/.test(w)) ?? words[0] ?? '?').slice(0, 3).toUpperCase().replace(/^(\D{2})\D+$/, '$1');
};

const R = 12, C = 2 * Math.PI * R;

export function PrintingChip() {
  const jobs = useStore((s) => JSON.stringify(s.printerConnections.connections.flatMap((c) => {
    const st = s.printerConnections.status[c.printer];
    return st?.online && (st.state === 'printing' || st.state === 'paused')
      ? [{ id: c.id, short: shortName(c.model || c.name), detail: `${c.name}: ${statusLine(st)}`, paused: st.state === 'paused', progress: st.progress ?? 0 }] : [];
  })));
  const list = JSON.parse(jobs) as { id: string; short: string; detail: string; paused: boolean; progress: number }[];
  if (!list.length) return null;
  return <span className="print-rings">{list.map((j) =>
    <button key={j.id} type="button" className="print-ring" data-paused={j.paused || undefined} onClick={openDevice}
      aria-label={`${j.detail}. Open Device`} data-truncated="true" data-full-text={j.detail}>
      <svg viewBox="0 0 28 28" aria-hidden="true">
        <circle className="print-ring-track" cx="14" cy="14" r={R} />
        <circle className="print-ring-done" cx="14" cy="14" r={R} strokeDasharray={C} strokeDashoffset={C * (1 - Math.min(1, Math.max(0, j.progress)))} />
      </svg>
      <span className="print-ring-name" data-long={j.short.length > 2 || undefined}>{j.short}</span>
    </button>)}</span>;
}
