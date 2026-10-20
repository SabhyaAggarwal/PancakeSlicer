import { useEffect, useState } from 'react';
import { exportFilename, outputKind } from '../actions';
import { mutation, responseError } from '../api/access';
import { useStore } from '../state/store';
import { Modal, ModalTitle } from './Modal';

type Transfer = { state: 'uploading' | 'starting' | 'done' | 'failed'; sent: number; total: number; message?: string; error?: string };
const LAST = 'orca.web.sendPrinter';
const mb = (n: number) => (n / 1e6).toFixed(1);

function statusText(t: Transfer) {
  if (t.state === 'uploading') return `Uploading ${Math.floor((100 * t.sent) / Math.max(1, t.total))}% (${mb(t.sent)} of ${mb(t.total)} MB)`;
  if (t.state === 'starting') return 'Starting the print...';
  return t.state === 'done' ? t.message ?? 'Sent.' : t.error ?? 'Sending failed.';
}

export default function SendDialog({ close }: { close: () => void }) {
  const jobId = useStore((s) => s.job.id);
  const bambu = useStore(outputKind) === 'gcode.3mf';
  const printers = useStore((s) => s.printers).filter((p) => (p.kind === 'bambu-lan') === bambu);
  const [printer, setPrinter] = useState(() => {
    const last = localStorage.getItem(LAST);
    return printers.find((p) => p.name === last)?.name ?? printers[0]?.name ?? '';
  });
  const [transfer, setTransfer] = useState<Transfer | null>(null);
  const [error, setError] = useState('');
  const busy = transfer?.state === 'uploading' || transfer?.state === 'starting';

  useEffect(() => {
    if (!busy || !jobId) return;
    const timer = setInterval(async () => {
      try {
        const r = await fetch(`/api/jobs/${jobId}/send`);
        if (!r.ok) throw await responseError(r);
        setTransfer(await r.json());
      } catch (e) {
        setTransfer(null);
        setError(e instanceof Error ? e.message : String(e));
      }
    }, 500);
    return () => clearInterval(timer);
  }, [busy, jobId]);

  const send = async (start: boolean) => {
    if (!jobId || !printer) return;
    setError('');
    localStorage.setItem(LAST, printer);
    try {
      const r = await mutation(`/api/jobs/${jobId}/send`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ printer, start, filename: exportFilename() }) });
      if (!r.ok) throw await responseError(r);
      setTransfer(await r.json());
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const raw = error || (transfer ? statusText(transfer) : '');
  const status = raw.charAt(0).toUpperCase() + raw.slice(1);
  const failed = !!error || transfer?.state === 'failed';
  return (
    <Modal className="small" label="Send G-code to printer host" onClose={close}>
      <ModalTitle title="Send G-code to printer host" onClose={close} />
      <div className="prefs-body">
        {printers.length ? (
          <label className="prefs-row">
            Printer
            <select className="combo" value={printer} disabled={busy} data-autofocus onChange={(e) => setPrinter(e.target.value)}>
              {printers.map((p) => <option key={p.name}>{p.name}</option>)}
            </select>
          </label>
        ) : (
          <div className="hint-text">No printer on this server takes {bambu ? '.gcode.3mf' : '.gcode'} files.</div>
        )}
        <div className="hint-text">{exportFilename()}</div>
        {busy && (
          <div className="progress">
            <div className="progress-fill" style={{ width: `${(100 * transfer.sent) / Math.max(1, transfer.total)}%` }} />
          </div>
        )}
        {status && <div role={failed ? 'alert' : 'status'} className="hint-text">{status}</div>}
      </div>
      <div className="modal-buttons center">
        <button type="button" className="btn primary" disabled={busy || !printer || !jobId} onClick={() => void send(true)}>Upload and Print</button>
        <button type="button" className="btn" disabled={busy || !printer || !jobId} onClick={() => void send(false)}>Upload</button>
        <button type="button" className="btn" onClick={close}>{transfer ? 'Close' : 'Cancel'}</button>
      </div>
    </Modal>
  );
}
