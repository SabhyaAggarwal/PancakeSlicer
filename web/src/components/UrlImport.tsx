import { useId } from 'react';
import { Check } from './Check';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';
import { checkAllUrlFiles, checkUrlFile, closeUrlImport, editImportUrl, importSelectedUrls, resolveUrlImport, urlImportBusy, useUrlImport } from '../url-import';
import './url-import.css';

function UrlImportForm() {
  const s = useUrlImport(), id = useId(), busy = urlImportBusy();
  return <form className="url-import" onSubmit={(e) => { e.preventDefault(); void (s.phase === 'picking' ? importSelectedUrls() : resolveUrlImport()); }} aria-busy={busy}>
    <div className="url-import-entry">
      <input autoFocus className="text" type="url" aria-label="Model URL" placeholder="Paste a model link" autoComplete="off" spellCheck={false} required value={s.url}
        disabled={busy} aria-invalid={!!s.error} aria-describedby={s.error ? `${id}-error` : undefined} onChange={(e) => editImportUrl(e.target.value)} />
      <button type="submit" className="btn-primary" disabled={busy || !s.url.trim() || s.phase === 'picking' && !s.checked.some(Boolean)}>{s.phase === 'picking' ? 'Import selected' : 'Load'}</button>
      {s.inline && <button type="button" className="btn-ghost" onClick={closeUrlImport}>{busy ? 'Cancel' : 'Close'}</button>}
    </div>
    {s.phase === 'picking' && <div className="modal-pick">
      <div className="modal-pick-head"><span>Choose model files</span><button type="button" className="btn-ghost" onClick={() => checkAllUrlFiles(true)}>Select all</button><button type="button" className="btn-ghost" onClick={() => checkAllUrlFiles(false)}>Clear</button></div>
      <div className="modal-pick-list">{s.files.map((file, i) => <label className="modal-check" key={`${file.key ?? file.url}-${i}`}>
        <Check checked={s.checked[i]} onCheckedChange={(checked) => checkUrlFile(i, checked)} /><span data-full-text={file.name}>{file.name}</span>
      </label>)}</div>
    </div>}
    {s.progress && <div className="url-import-progress" role="status" aria-live="polite">{s.progress}</div>}
    {s.error && <div id={`${id}-error`} className="url-import-error" role="alert">{s.error}</div>}
  </form>;
}
export function UrlImportInline() {
  const s = useUrlImport();
  return s.open && s.inline ? <UrlImportForm /> : null;
}
export function UrlImportHost() {
  const s = useUrlImport();
  return s.open && !s.inline ? <Modal size="s" label="Import from URL" onClose={closeUrlImport}>
    <ModalTitle title="Import from URL" onClose={closeUrlImport} />
    <ModalBody><UrlImportForm /></ModalBody>
    <ModalFooter><button type="button" className="btn" onClick={closeUrlImport}>{urlImportBusy() ? 'Cancel' : 'Close'}</button></ModalFooter>
  </Modal> : null;
}
