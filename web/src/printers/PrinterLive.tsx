import { setState, useStore } from '../state/store';
import { pickerStatus } from './connections';
import './printers.css';

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
    <button type="button" className="printer-live" onClick={open} aria-label={label ? `${label}. Printer status and Send` : 'Not connected. Connect this printer'}>
      <span className="printer-live-text">{label ? <span className="status-text" data-tone={tone}>{label}</span> : <><span className="status-text" data-tone="offline">Not connected</span> · <span className="printer-live-action">Connect</span></>}</span>
      {progress != null && <span className="printer-live-bar" aria-hidden><span style={{ width: `${100 * progress}%` }} /></span>}
    </button>
  );
}
