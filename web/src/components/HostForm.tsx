import { useState } from 'react';
import { loadPrinters } from '../actions';
import { mutation, responseError } from '../api/access';
import type { SendPrinter } from '../state/store';
import { Modal, ModalTitle } from './Modal';

type Test = { ok: boolean; stage: string; message: string };
const sentence = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
const errorText = (e: unknown) => (e instanceof Error ? e.message : String(e));
const HOST_TYPES: [string, string][] = [['prusalink', 'PrusaLink'], ['octoprint', 'OctoPrint']];

export default function HostForm({ printer, close, done }: { printer: SendPrinter | null; close: () => void; done: (name?: string) => void }) {
  const [kind, setKind] = useState(printer?.kind ?? 'prusalink');
  const [name, setName] = useState(printer?.name ?? '');
  const [url, setUrl] = useState(printer?.url?.replace(/^http:\/\//, '') ?? '');
  const [auth, setAuth] = useState(printer?.auth === 'digest' ? 'digest' : 'apikey');
  const [user, setUser] = useState(printer?.username ?? '');
  const [key, setKey] = useState('');
  const [test, setTest] = useState<Test | null>(null);
  const [error, setError] = useState('');
  const [working, setWorking] = useState('');
  const title = printer ? 'Edit printer' : 'Add printer';
  const digest = kind === 'prusalink' && auth === 'digest';
  const label = HOST_TYPES.find(([k]) => k === kind)![1];
  const fields = { kind, name: name.trim() || label, url, auth: kind === 'prusalink' ? auth : undefined, username: digest ? user : undefined, key };
  const ready = !!url.trim() && (!!key || !!printer) && (!digest || !!user.trim());
  const changed = <T,>(set: (v: T) => void) => (v: T) => { set(v); setTest(null); };

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
        <label className="prefs-row"><span>Name</span><input className="text" data-autofocus value={name} placeholder={label} onChange={(e) => setName(e.target.value)} /></label>
        <label className="prefs-row"><span>Host Type</span>
          <select className="combo" value={kind} disabled={!!printer} onChange={(e) => changed(setKind)(e.target.value)}>
            {HOST_TYPES.map(([k, l]) => <option key={k} value={k}>{l}</option>)}
          </select>
        </label>
        <label className="prefs-row"><span>Hostname, IP or URL</span><input className="text" value={url} placeholder="192.168.1.70" spellCheck={false} onChange={(e) => changed(setUrl)(e.target.value)} /></label>
        {kind === 'prusalink' && (
          <label className="prefs-row"><span>Authorization Type</span>
            <select className="combo" value={auth} onChange={(e) => changed(setAuth)(e.target.value)}>
              <option value="apikey">API key</option>
              <option value="digest">HTTP digest</option>
            </select>
          </label>
        )}
        {digest && <label className="prefs-row"><span>User</span><input className="text" value={user} autoComplete="off" spellCheck={false} onChange={(e) => changed(setUser)(e.target.value)} /></label>}
        <label className="prefs-row"><span>{digest ? 'Password' : 'API Key / Password'}</span>
          <input className="text" type="password" autoComplete="new-password" value={key} placeholder={printer ? 'Unchanged' : ''} onChange={(e) => changed(setKey)(e.target.value)} />
        </label>
        <div className="hint-text">The server connects only to addresses its administrator allows</div>
        {working === 'test' && <div role="status" className="hint-text">Testing...</div>}
        {test && (test.ok
          ? <div role="status" className="send-ok">{sentence(test.message)}.</div>
          : <div role="alert" className="warning-text">Could not connect to {label}: {test.message}</div>)}
        {error && <div role="alert" className="warning-text">{error}</div>}
      </div>
      <div className="modal-buttons center">
        <button type="button" className="btn" disabled={!ready || !!working} onClick={() => void runTest()}>Test</button>
        <button type="button" className="btn primary" disabled={!ready || !!working} onClick={() => void save()}>Save</button>
        {printer && <button type="button" className="btn" disabled={!!working} onClick={() => void remove()}>Remove</button>}
        <button type="button" className="btn" onClick={() => done()}>Cancel</button>
      </div>
    </Modal>
  );
}
