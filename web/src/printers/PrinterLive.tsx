import { setState, useStore } from '../state/store';
import { pickerStatus } from './connections';
import './printers.css';

const DOT = { online: 'online', printing: 'active', error: 'problem', offline: '' } as const;

export function PrinterLive() {
  const enabled = useStore((s) => s.printerConnections.enabled);
  const label = useStore((s) => pickerStatus(s.printerConnections, s.printer)?.label);
  const tone = useStore((s) => pickerStatus(s.printerConnections, s.printer)?.tone);
  const progress = useStore((s) => {
    const st = s.printerConnections.status[s.printer];
    return st?.online && (st.state === 'printing' || st.state === 'paused') ? st.progress : undefined;
  });
  if (!enabled) return null;
  const open = () => setState({ appDialog: 'send' });
  return (
    <button type="button" className="printer-live" onClick={open} title={label ? 'Printer status and Send' : 'Connect this printer'}>
      <span className={`printer-dot ${tone ? DOT[tone] : ''}`} aria-hidden />
      <span className="printer-live-text">{label ?? 'Not connected · Connect'}</span>
      {progress != null && <span className="printer-live-bar" aria-hidden><span style={{ width: `${100 * progress}%` }} /></span>}
    </button>
  );
}
