import { setState, useStore, type State } from '../state/store';
import { connectionOf, presenceStatus } from './connections';
import './printers.css';

export function PrinterLive() {
  const enabled = useStore((s) => s.printerConnections.enabled);
  const read = (s: State) => {
    const c = s.printerConnections, connection = connectionOf(c, s.printer);
    return connection && presenceStatus(c.live, connection, c.status[s.printer], true);
  };
  const label = useStore((s) => read(s)?.label), tone = useStore((s) => read(s)?.tone);
  const presence = label && tone ? { label, tone } : undefined;
  if (!enabled) return null;
  const open = () => setState({ appDialog: 'send' });
  const printing = presence?.label.startsWith('Printing') || presence?.label.startsWith('Paused');
  return (
    <button type="button" className="printer-live" onClick={open} aria-label={presence ? `${presence.label}. Printer status and Send` : 'Not connected — set up connection'}>
      <span className="printer-live-text">{presence
        ? printing ? <><span className="status-text printer-live-wide" data-tone="online">Online</span><span className="status-text printer-live-narrow" data-tone={presence.tone}>{presence.label}</span></>
          : <span className="status-text" data-tone={presence.tone}>{presence.label}</span>
        : <><span className="status-text" data-tone="offline">Not connected</span> <span className="printer-live-action">Connect</span></>}</span>
    </button>
  );
}
