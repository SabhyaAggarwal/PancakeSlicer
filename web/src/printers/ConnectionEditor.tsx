import { useMemo, useRef, useState } from 'react';
import { confirmDialog } from '../components/Ask';
import { Dropdown } from '../components/Dropdown';
import { getState, resolvePreset, toast, useStore } from '../state/store';
import {
  adoptConnection, connectionOf, discoverPrinters, KIND_LABEL, removeConnection, saveConnection, statusLine, tempLine, testConnection, unassigned,
  type ConnectionEdit, type ConnectionKind, type ConnectionTest, type Discovered, type PrinterStatus,
} from './connections';
import './printers.css';

const sentence = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
const errorText = (e: unknown) => sentence(e instanceof Error ? e.message : String(e));
const STAGE: Record<string, string> = {
  unreachable: 'Unreachable', serial: 'Wrong serial number', access_code: 'Wrong access code',
  developer_mode: 'Developer Mode is off', address: 'Address not allowed', invalid: 'Check the address',
};
const DESKTOP = typeof window !== 'undefined' && !!(window as { orcaDesktop?: unknown }).orcaDesktop;

function defaultKind(printer: string): ConnectionKind {
  const { base, config } = resolvePreset(getState(), 'machine', printer);
  if (base?.vendor === 'BBL' || /bambu/i.test(base?.name ?? '')) return 'bambu-lan';
  if (base?.vendor === 'Prusa') return 'prusalink';
  return String(config?.gcode_flavor ?? '') === 'klipper' ? 'moonraker' : 'octoprint';
}

export function statusClass(st: PrinterStatus | undefined) {
  if (!st?.online) return '';
  if (st.state === 'failed' || st.state === 'error') return 'problem';
  return st.state === 'printing' || st.state === 'paused' || st.state === 'busy' ? 'active' : 'online';
}

export function ConnectionEditor({ printer, name, onSaved }: { printer: string; name: string; onSaved?(): void }) {
  const saved = useStore((s) => connectionOf(s.printerConnections, printer));
  const status = useStore((s) => s.printerConnections.status[printer]);
  const all = useStore((s) => s.printerConnections);
  const earlier = useMemo(() => unassigned(all), [all]);
  const machines = useStore((s) => s.machines);
  const [kind, setKind] = useState<ConnectionKind>(saved?.kind ?? defaultKind(printer));
  const [host, setHost] = useState(saved?.host ?? '');
  const [serial, setSerial] = useState(saved?.serial ?? '');
  const [url, setUrl] = useState(saved?.url?.replace(/^http:\/\//, '') ?? '');
  const [auth, setAuth] = useState(saved?.auth === 'digest' ? 'digest' : 'apikey');
  const [user, setUser] = useState(saved?.username ?? '');
  const [secret, setSecret] = useState('');
  const [model, setModel] = useState(saved?.model ?? '');
  const [test, setTest] = useState<ConnectionTest | null>(null);
  const [found, setFound] = useState<{ printers: Discovered[]; notes: string[] } | null>(null);
  const [working, setWorking] = useState('');
  const [error, setError] = useState('');
  const sameKind = saved?.kind === kind;
  const bambu = kind === 'bambu-lan', http = !bambu && kind !== 'bambu-connect';
  const digest = kind === 'prusalink' && auth === 'digest';
  const keyOptional = kind === 'moonraker' || kind === 'duet';
  const edit = (): ConnectionEdit => ({ kind, name, model: model || undefined,
    ...(bambu ? { host: host.trim(), serial: serial.trim(), access_code: secret } : {}),
    ...(http ? { url: url.trim(), key: secret, auth: kind === 'prusalink' ? auth : undefined, username: digest ? user.trim() : undefined } : {}) });
  const hasSecret = !!saved?.has_secret && sameKind;
  const ready = kind === 'bambu-connect' || (bambu ? !!host.trim() && !!serial.trim() && (!!secret || hasSecret)
    : !!url.trim() && (keyOptional || !!secret || hasSecret) && (!digest || !!user.trim()));
  const fields = useRef(0);
  const touched = () => { fields.current++; setTest(null); };
  const changed = <T,>(set: (v: T) => void) => (v: T) => { set(v); touched(); };

  const run = async (what: string, action: () => Promise<void>) => {
    setWorking(what); setError('');
    try { await action(); } catch (e) { setError(errorText(e)); } finally { setWorking(''); }
  };
  const modelOf = (preset?: string) => preset ? machines.find((m) => m.name === preset)?.model : undefined;
  const setupModel = useStore((s) => String(resolvePreset(s, 'machine', printer).config?.printer_model ?? '')) || undefined;
  const use = async (d: Discovered) => {
    const found = modelOf(d.preset);
    if (found && setupModel && found !== setupModel && !await confirmDialog('Different printer model',
      `${d.model ?? found} at ${d.host ?? d.url} is not a ${setupModel}. G-code sliced with ${name} is made for a ${setupModel}; sent to this printer it can crash the head or print wrongly. Use it anyway?`,
      ['Use anyway', 'Cancel'], true)) return;
    setKind(d.kind); touched();
    if (d.kind === 'bambu-lan') { setHost(d.host ?? ''); setSerial(d.serial ?? ''); setModel(d.model ?? ''); }
    else setUrl((d.url ?? '').replace(/^http:\/\//, ''));
  };
  const addAsNew = (d: Discovered) => run('add', async () => {
    const { createProfileFromBase } = await import('../profiles/catalog');
    const key = await createProfileFromBase('machine', d.preset!, d.name || `My ${d.model ?? KIND_LABEL[d.kind]}`);
    const label = resolvePreset(getState(), 'machine', key).user?.name ?? d.name ?? key;
    if (d.kind !== 'bambu-lan') await saveConnection(key, { kind: d.kind, name: label, url: d.url });
    toast(d.kind === 'bambu-lan' ? `Added ${label}. Enter its access code in its Connection settings.` : `Added ${label} with its connection`);
  });

  return (
    <div className="connection-editor">
      {saved && (
        <div className="send-target" role="status">
          <span className={`printer-dot ${statusClass(status)}`} />
          <span>{statusLine(status) || 'Saved'}</span>
          {tempLine(status) && <span className="send-meta">{tempLine(status)}</span>}
        </div>
      )}
      <label className="prefs-row"><span>Host type</span>
        <Dropdown className="combo" value={kind} onValueChange={(v) => changed(setKind)(v as ConnectionKind)}
          options={(Object.keys(KIND_LABEL) as ConnectionKind[]).map((k) => ({ value: k, label: KIND_LABEL[k] }))} />
      </label>
      {bambu && <>
        <label className="prefs-row"><span>Printer IP</span><input className="text" value={host} placeholder="192.168.1.60" spellCheck={false} onChange={(e) => changed(setHost)(e.target.value)} /></label>
        <label className="prefs-row"><span>Serial number</span><input className="text" value={serial} spellCheck={false} onChange={(e) => changed(setSerial)(e.target.value)} /></label>
        <label className="prefs-row"><span>Access code</span><input className="text" type="password" autoComplete="new-password" value={secret} placeholder={hasSecret ? 'Stored' : ''} onChange={(e) => changed(setSecret)(e.target.value)} /></label>
        <div className="hint-text">On the printer: Settings › Network › LAN Only on, then Developer Mode on. The access code is shown there.</div>
      </>}
      {kind === 'bambu-connect' && <div className="hint-text">{DESKTOP
        ? 'Send hands the sliced file to Bambu Connect on this computer, which prints through your Bambu account. Use it when the printer stays in cloud mode.'
        : 'Bambu Connect runs on your computer: sending through it needs the desktop app. In the browser, download the .gcode.3mf and open it in Bambu Connect.'}</div>}
      {http && <>
        <label className="prefs-row"><span>Hostname, IP or URL</span><input className="text" value={url} placeholder={kind === 'moonraker' ? '192.168.1.70:7125' : '192.168.1.70'} spellCheck={false} onChange={(e) => changed(setUrl)(e.target.value)} /></label>
        {kind === 'prusalink' && (
          <label className="prefs-row"><span>Authorization</span>
            <Dropdown className="combo" value={auth} onValueChange={(v) => changed(setAuth)(v)} options={[{ value: 'apikey', label: 'API key' }, { value: 'digest', label: 'HTTP digest' }]} />
          </label>
        )}
        {digest && <label className="prefs-row"><span>User</span><input className="text" value={user} autoComplete="off" spellCheck={false} onChange={(e) => changed(setUser)(e.target.value)} /></label>}
        <label className="prefs-row"><span>{digest || kind === 'duet' ? 'Password' : 'API key'}{keyOptional ? ' (optional)' : ''}</span>
          <input className="text" type="password" autoComplete="new-password" value={secret} placeholder={hasSecret ? 'Stored' : ''} onChange={(e) => changed(setSecret)(e.target.value)} />
        </label>
      </>}
      {working === 'test' && <div role="status" className="hint-text">Testing…</div>}
      {test && (test.ok ? <div role="status" className="send-ok">{sentence(test.message)}</div>
        : <div role="alert" className="warning-text">{STAGE[test.stage] ? `${STAGE[test.stage]}: ` : ''}{test.message}</div>)}
      {error && <div role="alert" className="warning-text">{error}</div>}
      <div className="connection-buttons">
        <button type="button" className="btn" disabled={!ready || !!working} onClick={() => void run('test', async () => {
          const mine = fields.current;
          const result = await testConnection(printer, edit());
          if (mine !== fields.current) return;
          setTest(result);
          if (result.ok && result.model && bambu) setModel(result.model);
        })}>Test connection</button>
        <button type="button" className="btn primary" disabled={!ready || !!working} onClick={() => void run('save', async () => {
          await saveConnection(printer, edit()); setSecret(''); toast(`Saved the connection of ${name}`); onSaved?.();
        })}>Save</button>
        <button type="button" className="btn" disabled={!!working} onClick={() => void run('find', async () => setFound(await discoverPrinters()))}>{working === 'find' ? 'Searching…' : 'Find printers'}</button>
        {saved && <button type="button" className="btn" disabled={!!working} onClick={() => void run('remove', async () => { await removeConnection(printer); toast(`Removed the connection of ${name}`); })}>Remove connection</button>}
      </div>
      {found && (
        <div className="connection-found" aria-label="Printers found">
          {!found.printers.length && <div className="hint-text">No printers answered on the network.{found.notes.length ? ` ${found.notes.join(' ')}` : ''}</div>}
          {found.printers.map((d) => {
            const usedBy = d.assigned && d.assigned !== printer ? resolvePreset(getState(), 'machine', d.assigned).user?.name ?? 'another printer' : '';
            const preset = d.preset && machines.some((m) => m.name === d.preset);
            const other = modelOf(d.preset), mismatch = !!other && !!setupModel && other !== setupModel;
            return (
              <div className="connection-found-row" key={`${d.kind}${d.serial ?? ''}${d.host ?? ''}${d.url ?? ''}`}>
                <span>{[d.name, d.model, d.host ?? d.url?.replace(/^http:\/\//, ''), KIND_LABEL[d.kind], d.connection === 'cloud' ? 'cloud mode' : ''].filter(Boolean).join(' · ')}{usedBy ? ` · used by ${usedBy}` : ''}{mismatch ? ` · not a ${setupModel}` : ''}</span>
                <button type="button" className="btn" disabled={!!working} onClick={() => void use(d)} title={`Use for ${name}`}>Use</button>
                {preset && <button type="button" className="btn" disabled={!!working} onClick={() => void addAsNew(d)}>Add as new printer</button>}
              </div>
            );
          })}
        </div>
      )}
      {!saved && earlier.length > 0 && (
        <div className="connection-found" aria-label="Earlier printer connections">
          <div className="hint-text">Printers added before connections belonged to printer profiles</div>
          {earlier.map((c) => (
            <div className="connection-found-row" key={c.id}>
              <span>{[c.name, c.model, c.host ?? c.url?.replace(/^http:\/\//, ''), KIND_LABEL[c.kind]].filter(Boolean).join(' · ')}</span>
              <button type="button" className="btn" disabled={!!working} onClick={() => void run('adopt', async () => { await adoptConnection(printer, c.id, name); onSaved?.(); })} title={`Use for ${name}`}>Use</button>
              <button type="button" className="btn" disabled={!!working} onClick={() => void run('remove', () => removeConnection(`unassigned:${c.id}`))}>Remove</button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
