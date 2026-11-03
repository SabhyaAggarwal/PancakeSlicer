import { useEffect, useMemo, useState } from 'react';
import { Check } from './Check';
import { confirmDialog } from './Ask';
import { Dropdown } from './Dropdown';
import { Modal, ModalTitle } from './Modal';
import { download, exportFilename, outputKind } from '../actions';
import { printerSend, usePrinterSend, type Transfer } from '../flow/transfer';
import { filamentColor } from '../plater/filamentColor';
import { effectiveConfig, getState, resolvePreset, toast, useStore } from '../state/store';
import { ConnectionEditor, statusClass } from '../printers/ConnectionEditor';
import { connectionOf, connectionOutput, controlPrinter, KIND_LABEL, readTrays, statusLine, tempLine, type Tray } from '../printers/connections';
import '../printers/printers.css';

const EXTERNAL = 254;
const trayLabel = (t: Tray) => (t.id === EXTERNAL ? 'External spool' : `AMS ${t.ams + 1} slot ${t.slot + 1}`) + (t.type ? ` · ${t.type}` : ' · empty');
const rgb = (c: string) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16) || 0);
const distance = (a: string, b: string) => rgb(a).reduce((d, v, i) => d + (v - rgb(b)[i]) ** 2, 0);

function autoMap(filaments: { type: string; color: string }[], trays: Tray[]): number[] {
  const used = new Set<number>();
  return filaments.map((f) => {
    const same = trays.filter((t) => t.type && t.type.toUpperCase() === f.type.toUpperCase())
      .sort((a, b) => Number(used.has(a.id)) - Number(used.has(b.id)) || distance(a.color, f.color) - distance(b.color, f.color));
    const pick = same[0] ?? trays.find((t) => t.id === EXTERNAL) ?? trays[0];
    if (pick) used.add(pick.id);
    return pick?.id ?? EXTERNAL;
  });
}
const mb = (n: number) => (n / 1e6).toFixed(1);
const sentence = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
const errorText = (e: unknown) => sentence(e instanceof Error ? e.message : String(e));
const setupName = (key: string) => resolvePreset(getState(), 'machine', key).user?.name ?? key;

function transferText(t: Transfer) {
  if (t.state === 'uploading') return `Uploading ${Math.floor((100 * t.sent) / Math.max(1, t.total))}% (${mb(t.sent)} of ${mb(t.total)} MB)`;
  if (t.state === 'starting') return 'Starting the print…';
  return t.state === 'done' ? t.message ?? 'Sent.' : t.error ?? 'Sending failed.';
}

export default function SendDialog({ close }: { close: () => void }) {
  const plate = useStore((s) => s.printer);
  const choices = useStore((s) => {
    const base = resolvePreset(s, 'machine', s.printer).base?.name;
    return [s.printer, ...s.printerConnections.connections.filter((c) => c.printer && c.printer !== s.printer
      && resolvePreset(s, 'machine', c.printer).base?.name === base).map((c) => c.printer)].join('\n');
  }).split('\n');
  const [target, setTarget] = useState(plate);
  const printer = choices.includes(target) ? target : plate;
  const name = useStore(() => setupName(printer));
  const connection = useStore((s) => connectionOf(s.printerConnections, printer));
  const status = useStore((s) => s.printerConnections.status[printer]);
  const [editing, setEditing] = useState(false);
  const title = 'Send to printer';
  return (
    <Modal className="small send-dialog" label={title} onClose={close}>
      <ModalTitle title={title} onClose={close} />
      <div className="prefs-body send-body">
        <div className="send-target">
          <span className={`printer-dot ${statusClass(status)}`} aria-hidden />
          {choices.length > 1
            ? <Dropdown className="combo" value={printer} aria-label="Printer" onValueChange={setTarget} options={choices.map((k) => ({ value: k, label: setupName(k) }))} />
            : <span className="send-target-name">{name}</span>}
          {connection && <button type="button" className="btn" onClick={() => setEditing(!editing)}>{editing ? 'Done' : 'Connection'}</button>}
        </div>
        {connection && <div className="send-meta">{[KIND_LABEL[connection.kind], connection.host ?? connection.url?.replace(/^https?:\/\//, ''), statusLine(status), tempLine(status)].filter(Boolean).join(' · ')}</div>}
        {!connection && <div className="hint-text">{name} has no connection yet. Find it on the network or enter its address.</div>}
        {(!connection || editing) && <ConnectionEditor key={printer} printer={printer} name={name} onSaved={() => setEditing(false)} />}
      </div>
      {connection && !editing ? <SendView close={close} printer={printer} /> : (
        <div className="modal-buttons center"><button type="button" className="btn" onClick={close}>Close</button></div>
      )}
    </Modal>
  );
}

function SendView({ close, printer }: { close: () => void; printer: string }) {
  const connection = useStore((s) => connectionOf(s.printerConnections, printer))!;
  const status = useStore((s) => s.printerConnections.status[printer]);
  const jobId = useStore((s) => s.job.id);
  const jobReady = useStore((s) => s.job.state === 'done' && !s.job.stale);
  const plateIndex = useStore((s) => s.plateIndex);
  const output = useStore(outputKind);
  const kind = connection.kind, bambu = kind === 'bambu-lan';
  const fits = connectionOutput(kind) === output;
  const single = useStore((s) => s.filamentSlots.length === 1);
  const [leveling, setLeveling] = useState(true);
  const [flowCali, setFlowCali] = useState(false);
  const [timelapse, setTimelapse] = useState(false);
  const filamentKey = useStore((s) => JSON.stringify(s.filamentSlots.map((_, i) => ({ type: String(effectiveConfig(s, 'filament', i).filament_type ?? ''), color: filamentColor(s, i + 1) }))));
  const filaments = useMemo(() => JSON.parse(filamentKey) as { type: string; color: string }[], [filamentKey]);
  const liveTrays = status?.online ? status.trays : undefined;
  const [readTraysList, setReadTrays] = useState<Tray[] | null>(null);
  const trays = liveTrays ?? readTraysList;
  const [amsError, setAmsError] = useState('');
  const [mapping, setMapping] = useState<number[] | null>(null);
  const shown = mapping && mapping.length === filaments.length ? mapping : trays ? autoMap(filaments, trays) : [];
  const sendState = usePrinterSend();
  const { transfer, error } = sendState.context;
  const busy = sendState.value === 'connecting' || sendState.value === 'sending';
  const [working, setWorking] = useState('');

  useEffect(() => {
    const current = printerSend.machine.get().context;
    if (current.jobId && (current.jobId !== jobId || current.target !== printer) && printerSend.machine.get().value !== 'sending') printerSend.reset();
  }, [jobId, printer]);
  useEffect(() => {
    setReadTrays(null); setAmsError(''); setMapping(null);
    if (!bambu || liveTrays) return;
    let live = true;
    readTrays(printer).then((list) => { if (live) setReadTrays(list); }, (e) => { if (live) setAmsError(errorText(e)); });
    return () => { live = false; };
  }, [bambu, printer, !!liveTrays]);

  const send = async (start: boolean) => {
    if (!jobId) return;
    const options = !bambu ? {} : { bed_leveling: leveling, flow_cali: flowCali, timelapse, ...(trays && shown.length === filaments.length ? { ams_mapping: shown } : {}) };
    await printerSend.submit(printer, jobId, { start, filename: exportFilename(), ...options });
  };
  const handOff = async () => {
    if (!jobId) return;
    setWorking('connect');
    try { await window.orcaDesktop!.bambuConnect!(jobId, exportFilename()); toast('Opened in Bambu Connect'); }
    catch (e) { toast(errorText(e)); } finally { setWorking(''); }
  };
  const control = async (action: 'stop' | 'pause' | 'resume') => {
    if (action === 'stop' && !await confirmDialog('Stop print', `Stop the print on ${setupName(printer)}? It cannot be resumed.`, ['Stop print', 'Cancel'], true)) return;
    setWorking(action);
    try { await controlPrinter(printer, action); } catch (e) { toast(errorText(e)); } finally { setWorking(''); }
  };

  const mine = transfer && transfer.printer === printer && transfer.job === jobId ? transfer : null;
  const statusText = error || (mine ? transferText(mine) : sendState.value === 'connecting' ? 'Connecting to printer…' : '');
  const failed = !!error || mine?.state === 'failed' || mine?.state === 'cancelled';
  const running = status?.online && (status.state === 'printing' || status.state === 'paused');
  const blocked = !jobId || !jobReady ? 'Slice the plate first.' : !fits ? `${KIND_LABEL[kind]} takes .${connectionOutput(kind)} files; this printer profile exports .${output}.` : '';
  const needsMap = bambu && !single && !trays;
  return <>
    <div className="prefs-body send-body">
      <div className="send-meta">Plate {plateIndex} · {exportFilename()}</div>
      {blocked && <div className="warning-text">{blocked}</div>}
      {bambu && fits && (
        <div className="send-options">
          <label className="prefs-row"><Check checked={leveling} disabled={busy} onCheckedChange={setLeveling} />Bed leveling</label>
          <label className="prefs-row"><Check checked={flowCali} disabled={busy} onCheckedChange={setFlowCali} />Flow dynamics calibration</label>
          <label className="prefs-row"><Check checked={timelapse} disabled={busy} onCheckedChange={setTimelapse} />Timelapse</label>
        </div>
      )}
      {bambu && fits && (
        <div className="ams-map" aria-label="Filament mapping">
          {trays ? filaments.map((f, i) => {
            const t = trays.find((x) => x.id === shown[i]);
            return (
              <label key={i} className="prefs-row">
                <span className="ams-swatch" style={{ background: f.color }} />
                <span>{i + 1} · {f.type || 'filament'}</span>
                <Dropdown className="combo" value={shown[i] ?? EXTERNAL} disabled={busy} aria-label={`Filament ${i + 1}`}
                  onValueChange={(next) => setMapping(shown.map((m, k) => (k === i ? Number(next) : m)))} options={trays.map((x) => ({ value: x.id, label: trayLabel(x) }))} />
                {t?.color && <span className="ams-swatch" aria-label={trayLabel(t)} role="img" style={{ background: t.color }} />}
              </label>
            );
          }) : <div className={amsError ? 'warning-text' : 'hint-text'}>{amsError ? `AMS not read: ${amsError}` : 'Reading the printer\'s AMS…'}</div>}
          {needsMap && amsError && <div className="hint-text">A plate with several filaments needs the AMS mapping to send.</div>}
        </div>
      )}
      {busy && (
        <div className="progress-row">
          <div className="progress"><div className="progress-fill" style={{ width: `${(100 * (mine?.sent ?? 0)) / Math.max(1, mine?.total ?? 1)}%` }} /></div>
          <button type="button" className="btn" disabled={mine?.state !== 'uploading'} onClick={() => printerSend.cancel()}>Cancel upload</button>
        </div>
      )}
      {statusText && <div role={failed ? 'alert' : 'status'} className={failed ? 'warning-text' : 'hint-text'}>{sentence(statusText)}</div>}
      {running && (
        <div className="send-live" aria-label="Printer status">
          <div className="send-live-row">
            <span>{statusLine(status)}{status!.file ? ` · ${status!.file}` : ''}</span>
            <button type="button" className="btn" disabled={!!working} onClick={() => void control(status!.state === 'paused' ? 'resume' : 'pause')}>{status!.state === 'paused' ? 'Resume' : 'Pause'}</button>
            <button type="button" className="btn" disabled={!!working} onClick={() => void control('stop')}>Stop</button>
          </div>
          {status!.progress != null && <div className="progress"><div className="progress-fill" style={{ width: `${100 * status!.progress}%` }} /></div>}
          {(tempLine(status) || status!.layers) && <div className="send-live-row">{[tempLine(status), status!.layers ? `Layer ${status!.layer ?? 0} of ${status!.layers}` : ''].filter(Boolean).join(' · ')}</div>}
        </div>
      )}
    </div>
    <div className="modal-buttons center">
      {sendState.value === 'failed' && mine && <button type="button" className="btn" onClick={() => void printerSend.retry()}>Reconnect</button>}
      {kind === 'bambu-connect' ? (window.orcaDesktop?.bambuConnect
        ? <button type="button" className="btn primary" disabled={!!blocked || !!working} onClick={() => void handOff()}>Open in Bambu Connect</button>
        : <button type="button" className="btn primary" disabled={!!blocked} onClick={() => void download()}>Download .gcode.3mf</button>) : <>
        <button type="button" className="btn primary" disabled={busy || !!blocked || needsMap || running} title={running ? 'The printer is printing' : undefined} onClick={() => void send(true)}>Print</button>
        <button type="button" className="btn" disabled={busy || !!blocked || needsMap} onClick={() => void send(false)}>Upload only</button>
      </>}
      <button type="button" className="btn" onClick={close}>Close</button>
    </div>
  </>;
}
