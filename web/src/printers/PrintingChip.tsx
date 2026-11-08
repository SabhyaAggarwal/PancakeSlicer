import { useStore } from '../state/store';
import { openDevice } from './sendOutcome';
import './printers.css';

export function PrintingChip() {
  const jobs = useStore((s) => JSON.stringify(s.printerConnections.connections.flatMap((c) => {
    const st = s.printerConnections.status[c.printer];
    return st?.online && (st.state === 'printing' || st.state === 'paused') ? [{ name: c.name, paused: st.state === 'paused', progress: st.progress }] : [];
  })));
  const list = JSON.parse(jobs) as { name: string; paused: boolean; progress?: number }[];
  if (!list.length) return null;
  const [first] = list, percent = first.progress != null ? `${Math.round(first.progress * 100)}%` : '';
  const label = `${first.paused ? 'Paused' : 'Printing'} on ${first.name}${percent ? `, ${percent}` : ''}${list.length > 1 ? `, and ${list.length - 1} more` : ''}`;
  return <button type="button" className="printing-chip" data-paused={first.paused || undefined} onClick={openDevice} title={`${label}. Open Device`} aria-label={`${label}. Open Device`}>
    <span className="printing-chip-name">{first.paused ? 'Paused' : 'Printing'} on {first.name}</span>
    {percent && <span className="printing-chip-percent">{percent}</span>}
    {first.progress != null && <span className="printing-chip-bar" aria-hidden="true"><span style={{ width: percent }} /></span>}
  </button>;
}
