import { useEffect, useMemo, useState } from 'react';
import { download, exportFilename, loadPrinters, outputKind } from '../actions';
import { mutation, responseError } from '../api/access';
import { effectiveConfig, useStore, type SendPrinter } from '../state/store';
import { filamentColor } from '../plater/filamentColor';
import { Modal, ModalTitle } from './Modal';

type Transfer = { state: 'uploading' | 'starting' | 'done' | 'failed'; sent: number; total: number; message?: string; error?: string };
type Test = { ok: boolean; stage: string; message: string; model?: string };
type Tray = { id: number; ams: number; slot: number; type: string; color: string; name?: string };
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
const LAST = 'orca.web.sendPrinter';
const mb = (n: number) => (n / 1e6).toFixed(1);
const sentence = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
const errorText = (e: unknown) => (e instanceof Error ? e.message : String(e));
const STAGE: Record<string, string> = {
  unreachable: 'Unreachable', serial: 'Wrong serial number',
  access_code: 'Wrong access code', developer_mode: 'LAN-only mode / Developer Mode off',
};

function statusText(t: Transfer) {
  if (t.state === 'uploading') return `Uploading ${Math.floor((100 * t.sent) / Math.max(1, t.total))}% (${mb(t.sent)} of ${mb(t.total)} MB)`;
  if (t.state === 'starting') return 'Starting the print...';
  return t.state === 'done' ? t.message ?? 'Sent.' : t.error ?? 'Sending failed.';
}

export default function SendDialog({ close }: { close: () => void }) {
  const [editing, setEditing] = useState<SendPrinter | null | undefined>(undefined);
  const [chosen, setChosen] = useState(() => localStorage.getItem(LAST) ?? '');
  if (editing !== undefined) {
    return <PrinterForm printer={editing} close={close} done={(name) => { if (name !== undefined) setChosen(name); setEditing(undefined); }} />;
  }
  return <SendView close={close} chosen={chosen} choose={setChosen} edit={setEditing} />;
}

function SendView({ close, chosen, choose, edit }: { close: () => void; chosen: string; choose: (name: string) => void; edit: (p: SendPrinter | null) => void }) {
  const jobId = useStore((s) => s.job.id);
  const bambu = useStore(outputKind) === 'gcode.3mf';
  const canAdd = useStore((s) => s.printerEdit) && bambu;
  const single = useStore((s) => s.filamentSlots.length === 1);
  const printers = useStore((s) => s.printers).filter((p) => (p.kind === 'bambu-lan') === bambu);
  const current = printers.find((p) => p.name === chosen) ?? printers[0];
  const printer = current?.name ?? '';
  const [leveling, setLeveling] = useState(true);
  const [timelapse, setTimelapse] = useState(false);
  const [ams, setAms] = useState(false);
  const filamentKey = useStore((s) => JSON.stringify(s.filamentSlots.map((_, i) => ({ type: String(effectiveConfig(s, 'filament', i).filament_type ?? ''), color: filamentColor(s, i + 1) }))));
  const filaments = useMemo(() => JSON.parse(filamentKey) as { type: string; color: string }[], [filamentKey]);
  const [trays, setTrays] = useState<Tray[] | null>(null);
  const [amsError, setAmsError] = useState('');
  const [mapping, setMapping] = useState<number[]>([]);
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
        setError(errorText(e));
      }
    }, 500);
    return () => clearInterval(timer);
  }, [busy, jobId]);

  useEffect(() => {
    setTrays(null);
    setAmsError('');
    if (!bambu || !printer) return;
    let live = true;
    void (async () => {
      try {
        const r = await mutation(`/api/printers/${encodeURIComponent(printer)}/ams`, { method: 'POST' });
        if (!r.ok) throw await responseError(r);
        const list = ((await r.json()).trays ?? []) as Tray[];
        if (!live) return;
        setTrays(list);
        setMapping(autoMap(JSON.parse(filamentKey), list));
      } catch (e) {
        if (live) setAmsError(errorText(e));
      }
    })();
    return () => { live = false; };
  }, [bambu, printer]);

  const send = async (start: boolean) => {
    if (!jobId || !printer) return;
    setError('');
    localStorage.setItem(LAST, printer);
    const options = !bambu ? {} : trays && mapping.length === filaments.length ? { bed_leveling: leveling, timelapse, ams_mapping: mapping }
      : { bed_leveling: leveling, timelapse, use_ams: single && ams };
    try {
      const r = await mutation(`/api/jobs/${jobId}/send`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ printer, start, filename: exportFilename(), ...options }) });
      if (!r.ok) throw await responseError(r);
      setTransfer(await r.json());
    } catch (e) {
      setError(errorText(e));
    }
  };

  const status = sentence(error || (transfer ? statusText(transfer) : ''));
  const failed = !!error || transfer?.state === 'failed';
  const title = bambu ? 'Send print job to' : 'Send G-code to printer host';
  return (
    <Modal className="small send-dialog" label={title} onClose={close}>
      <ModalTitle title={title} onClose={close} />
      <div className="prefs-body send-body">
        {printers.length ? (
          <label className="prefs-row">
            <span>Printer</span>
            <select className="combo" value={printer} disabled={busy} data-autofocus onChange={(e) => choose(e.target.value)}>
              {printers.map((p) => <option key={p.name} value={p.name}>{p.model && p.model !== p.name ? `${p.name} (${p.model})` : p.name}</option>)}
            </select>
            {current?.editable && <button type="button" className="btn" disabled={busy} onClick={() => edit(current)}>Edit</button>}
            {canAdd && <button type="button" className="btn" disabled={busy} onClick={() => edit(null)}>Add</button>}
          </label>
        ) : canAdd ? (
          <div className="prefs-row"><span className="hint-text">No printer yet</span><button type="button" className="btn primary" data-autofocus onClick={() => edit(null)}>Add printer</button></div>
        ) : (
          <div className="hint-text">No printer on this server takes {bambu ? '.gcode.3mf' : '.gcode'} files.</div>
        )}
        <div className="hint-text">{exportFilename()}</div>
        {bambu && printers.length > 0 && (
          <div className="send-options">
            <label className="prefs-row"><input type="checkbox" checked={leveling} disabled={busy} onChange={(e) => setLeveling(e.target.checked)} />Bed Leveling</label>
            <label className="prefs-row"><input type="checkbox" checked={timelapse} disabled={busy} onChange={(e) => setTimelapse(e.target.checked)} />Timelapse</label>
            {!trays && single && <label className="prefs-row" title="Print filament 1 from AMS slot 1; off prints from the external spool"><input type="checkbox" checked={ams} disabled={busy} onChange={(e) => setAms(e.target.checked)} />Use AMS (slot 1)</label>}
          </div>
        )}
        {bambu && printers.length > 0 && (
          <div className="ams-map" aria-label="Filament mapping">
            {trays ? filaments.map((f, i) => (
              <label key={i} className="prefs-row">
                <span className="ams-swatch" style={{ background: f.color }} />
                <span>{i + 1} · {f.type || 'filament'}</span>
                <select className="combo" value={mapping[i] ?? EXTERNAL} disabled={busy} aria-label={`Filament ${i + 1}`}
                  onChange={(e) => setMapping(mapping.map((m, k) => (k === i ? Number(e.target.value) : m)))}>
                  {trays.map((t) => <option key={t.id} value={t.id}>{trayLabel(t)}</option>)}
                </select>
                {(() => { const t = trays.find((x) => x.id === mapping[i]); return t?.color ? <span className="ams-swatch" title={trayLabel(t)} style={{ background: t.color }} /> : null; })()}
              </label>
            )) : <div className={amsError ? 'warning-text' : 'hint-text'}>{amsError ? `AMS not read: ${amsError}` : 'Reading the printer\'s AMS…'}</div>}
            {!trays && !single && amsError && <div className="hint-text">A job with several filaments needs the AMS mapping to send.</div>}
          </div>
        )}
        {bambu && <div className="hint-text">The printer must be in LAN-only mode with Developer Mode on</div>}
        {busy && (
          <div className="progress">
            <div className="progress-fill" style={{ width: `${(100 * transfer.sent) / Math.max(1, transfer.total)}%` }} />
          </div>
        )}
        {status && <div role={failed ? 'alert' : 'status'} className={failed ? 'warning-text' : 'hint-text'}>{status}</div>}
      </div>
      <div className="modal-buttons center">
        <button type="button" className="btn primary" disabled={busy || !printer || !jobId || bambu && !single && !trays} onClick={() => void send(true)}>Upload and Print</button>
        <button type="button" className="btn" disabled={busy || !printer || !jobId || bambu && !single && !trays} onClick={() => void send(false)}>Upload</button>
        {bambu && <button type="button" className="btn" onClick={() => void download()}>Download .gcode.3mf instead</button>}
        <button type="button" className="btn" onClick={close}>{transfer ? 'Close' : 'Cancel'}</button>
      </div>
    </Modal>
  );
}

function PrinterForm({ printer, close, done }: { printer: SendPrinter | null; close: () => void; done: (name?: string) => void }) {
  const [name, setName] = useState(printer?.name ?? '');
  const [host, setHost] = useState(printer?.host ?? '');
  const [serial, setSerial] = useState(printer?.serial ?? '');
  const [code, setCode] = useState('');
  const [test, setTest] = useState<Test | null>(null);
  const [error, setError] = useState('');
  const [working, setWorking] = useState('');
  const title = printer ? 'Edit printer' : 'Add printer';
  const fields = { name: name.trim() || test?.model || 'Bambu printer', host, serial, access_code: code, model: test?.ok ? test.model : undefined };
  const ready = !!host.trim() && !!serial.trim() && (!!code || !!printer);

  const run = async (what: string, request: () => Promise<Response>, then: (r: Response) => Promise<void>) => {
    setWorking(what);
    setError('');
    try {
      const r = await request();
      if (!r.ok) throw await responseError(r);
      await then(r);
    } catch (e) {
      setError(sentence(errorText(e)));
    } finally {
      setWorking('');
    }
  };
  const json = (method: string, body?: object) => ({ method, headers: { 'Content-Type': 'application/json' }, body: body && JSON.stringify(body) });
  const runTest = () => run('test', () => mutation('/api/printers/test', json('POST', { ...fields, name: printer?.name ?? '' })), async (r) => setTest(await r.json()));
  const save = () => run('save', () => mutation(printer ? `/api/printers/${encodeURIComponent(printer.name)}` : '/api/printers', json(printer ? 'PUT' : 'POST', fields)), async () => {
    await loadPrinters();
    done(fields.name);
  });
  const remove = () => run('remove', () => mutation(`/api/printers/${encodeURIComponent(printer!.name)}`, json('DELETE')), async () => {
    await loadPrinters();
    done('');
  });

  return (
    <Modal className="small send-dialog" label={title} onClose={close}>
      <ModalTitle title={title} onClose={close} />
      <div className="prefs-body send-body printer-form">
        <label className="prefs-row"><span>Name</span><input className="text" data-autofocus value={name} placeholder={test?.model ?? 'Bambu printer'} onChange={(e) => setName(e.target.value)} /></label>
        <label className="prefs-row"><span>Printer IP</span><input className="text" value={host} placeholder="192.168.1.60" inputMode="decimal" onChange={(e) => { setHost(e.target.value); setTest(null); }} /></label>
        <label className="prefs-row"><span>Printer SN</span><input className="text" value={serial} spellCheck={false} onChange={(e) => { setSerial(e.target.value); setTest(null); }} /></label>
        <label className="prefs-row"><span>Access Code</span><input className="text" type="password" autoComplete="new-password" value={code} placeholder={printer ? 'Unchanged' : ''} onChange={(e) => { setCode(e.target.value); setTest(null); }} /></label>
        <div className="hint-text">The printer must be in LAN-only mode with Developer Mode on</div>
        {working === 'test' && <div role="status" className="hint-text">Testing...</div>}
        {test && (test.ok
          ? <div role="status" className="send-ok">{sentence(test.message)}</div>
          : <div role="alert" className="warning-text">{STAGE[test.stage] ? `${STAGE[test.stage]}: ${test.message}` : sentence(test.message)}</div>)}
        {error && <div role="alert" className="warning-text">{error}</div>}
      </div>
      <div className="modal-buttons center">
        <button type="button" className="btn" disabled={!ready || !!working} onClick={() => void runTest()}>Test connection</button>
        <button type="button" className="btn primary" disabled={!ready || !!working} onClick={() => void save()}>Save</button>
        {printer && <button type="button" className="btn" disabled={!!working} onClick={() => void remove()}>Remove</button>}
        <button type="button" className="btn" onClick={() => done()}>Cancel</button>
      </div>
    </Modal>
  );
}
