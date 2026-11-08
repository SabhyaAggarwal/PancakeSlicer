import { useId } from 'react';
import { Check } from './Check';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';
import { checkAllUrlFiles, checkUrlFile, closeUrlImport, editImportUrl, importSelectedUrls, resolveUrlImport, urlImportBusy, useUrlImport } from '../url-import';
import './url-import.css';

function Actions({ form }: { form: string }) {
  const s = useUrlImport(), busy = urlImportBusy(), picked = s.checked.filter(Boolean).length;
  return <>
    <button type="button" className="btn-ghost" onClick={closeUrlImport}>Cancel</button>
    <button type="submit" form={form} className="btn-primary" disabled={busy || !s.url.trim() || s.phase === 'picking' && !picked}>
      {s.phase === 'picking' ? `Import ${picked} ${picked === 1 ? 'file' : 'files'}` : 'Load'}
    </button>
  </>;
}
function UrlImportForm({ form, inline }: { form: string; inline: boolean }) {
  const s = useUrlImport(), busy = urlImportBusy(), all = s.checked.every(Boolean);
  return <form id={form} className="url-import" onSubmit={(e) => { e.preventDefault(); void (s.phase === 'picking' ? importSelectedUrls() : resolveUrlImport()); }} aria-busy={busy}>
    <input autoFocus className="text" type="url" aria-label="Model URL" placeholder="Paste a model link" autoComplete="off" spellCheck={false} required value={s.url}
      disabled={busy} aria-invalid={!!s.error} aria-describedby={s.error ? `${form}-error` : undefined} onChange={(e) => editImportUrl(e.target.value)} />
    {s.phase === 'picking' && <div className="url-import-pick" role="group" aria-labelledby={`${form}-files`}>
      <div className="url-import-head">
        <span id={`${form}-files`}>{s.files.length} files</span>
        <button type="button" className="btn-ghost" onClick={() => checkAllUrlFiles(!all)}>{all ? 'Clear' : 'Select all'}</button>
      </div>
      <div className="url-import-list">{s.files.map((file, i) => <label className="url-import-file" key={`${file.key ?? file.url}-${i}`} title={file.name}>
        <Check checked={s.checked[i]} onCheckedChange={(checked) => checkUrlFile(i, checked)} /><span>{file.name}</span>
      </label>)}</div>
    </div>}
    {s.progress && <div className="url-import-progress" role="status" aria-live="polite">{s.progress}</div>}
    {s.error && <div id={`${form}-error`} className="url-import-error" role="alert">{s.error}</div>}
    {inline && <div className="url-import-actions"><Actions form={form} /></div>}
  </form>;
}
export function UrlImportInline() {
  const s = useUrlImport(), form = useId();
  return s.open && s.inline ? <UrlImportForm form={form} inline /> : null;
}
export function UrlImportHost() {
  const s = useUrlImport(), form = useId();
  return s.open && !s.inline ? <Modal size="s" label="Import from URL" onClose={closeUrlImport}>
    <ModalTitle title="Import from URL" onClose={closeUrlImport} />
    <ModalBody><UrlImportForm form={form} inline={false} /></ModalBody>
    <ModalFooter><Actions form={form} /></ModalFooter>
  </Modal> : null;
}
