import { Check } from './Check';
import { useRef, useState } from 'react';
import type { PresetType,UserPreset } from '../api/types';
import { applyImportPlans, downloadArchive, filamentAlias, filamentBundle, planImport, presetZip, printerBundle, readPresetFile, type ExportKind, type ImportPlan } from '../preset-io';
import { getState, setState, toast, useStore } from '../state/store';
import { Modal, ModalTitle } from './Modal';

const TYPE_NAME: Record<PresetType, string> = { machine: 'Printer', filament: 'Filament', process: 'Process' };
const ACCEPT = '.json,.zip,.bbscfg,.orca_printer,.orca_filament,.orca_bundle';
const close = () => setState({ presetUx: null });

type Row = ImportPlan & { checked: boolean; result?: string; ok?: boolean };

export function ImportDialog() {
  const [rows, setRows] = useState<Row[] | null>(null);
  const [busy, setBusy] = useState(false);
  const [done, setDone] = useState(false);
  const [drag, setDrag] = useState(false);
  const input = useRef<HTMLInputElement>(null);

  const read = async (files: FileList | File[]) => {
    const list = [...files];
    if (!list.length) return;
    setBusy(true);
    const parsed = (await Promise.all(list.map(readPresetFile))).flat();
    setRows(planImport(getState(), parsed).map((p) => ({ ...p, checked: p.status !== 'skip' })));
    setDone(false);
    setBusy(false);
  };

  const run = async () => {
    if(!rows)return;
    setBusy(true);
    const chosen=rows.filter(r=>r.checked && (r.preset || r.base));
    try {
      const result=await applyImportPlans(chosen);
      setRows(rows.map(row=>chosen.includes(row) ? {...row,ok:true,result:row.status === 'replace' ? 'replaced' : 'imported'} : row));
      setDone(true);toast(`Imported ${result.profiles} profiles and ${result.bases} templates`);
    }catch(error){setRows(rows.map(row=>chosen.includes(row) ? {...row,ok:false,result:error instanceof Error ? error.message : String(error)} : row));}
    finally {setBusy(false);}
  };

  const count = rows?.filter((r) => r.checked && (r.preset || r.base)).length ?? 0;
  return (
    <Modal className="np-dialog import-dialog" label="Import Configs" onClose={close}>
      <ModalTitle title="Import Configs" onClose={close} />
      <div className={`np-dialog-body import-drop ${drag ? 'drag' : ''}`}
        onDragOver={(e) => { e.preventDefault(); setDrag(true); }} onDragLeave={() => setDrag(false)}
        onDrop={(e) => { e.preventDefault(); e.stopPropagation(); setDrag(false); void read(e.dataTransfer.files); }}>
        <div className="row">
          <button type="button" className="btn primary" data-autofocus onClick={() => input.current?.click()} disabled={busy}>Choose files…</button>
          <input ref={input} type="file" multiple hidden accept={ACCEPT} aria-label="Preset files" onChange={(e) => { if (e.target.files) void read(e.target.files); e.target.value = ''; }} />
        </div>
        {busy && <div role="status">{done ? 'Importing…' : 'Reading…'}</div>}
        {rows && !rows.length && <div className="hint-text">No presets</div>}
        {rows && rows.length > 0 && (
          <div className="diff-scroll">
            <table className="np-table">
              <thead><tr><th><Check aria-label="Select all" disabled={done} checked={rows.every((r) => r.checked || !(r.preset || r.base))}
                onCheckedChange={(next) => setRows(rows.map((r) => ({ ...r, checked: !!(r.preset || r.base) && next })))} /></th>
                <th>Preset</th><th>Type</th><th>Parent</th><th>{done ? 'Result' : 'Action'}</th></tr></thead>
              <tbody>{rows.map((r, i) => (
                <tr key={`${r.parsed.file}:${i}`} title={r.parsed.file}>
                  <td><Check aria-label={`Import ${r.parsed.name}`} disabled={!(r.preset || r.base) || done} checked={r.checked}
                    onCheckedChange={(next) => setRows(rows.map((x, j) => (j === i ? { ...x, checked: next } : x)))} /></td>
                  <td>{r.parsed.name}</td>
                  <td>{r.parsed.type ? TYPE_NAME[r.parsed.type] : '—'}</td>
                  <td>{r.preset?.inherits ?? r.parsed.inherits ?? ''}</td>
                  <td className={r.ok === false ? 'np-error' : r.status === 'replace' && !done ? 'np-warn-text' : undefined}>
                    {done ? (r.result ?? (r.preset ? 'not selected' : `skipped: ${r.note}`))
                      : r.status === 'skip' ? `skip: ${r.note}` : `${r.status === 'replace' ? 'overwrite' : 'import'}${r.note ? ` (${r.note})` : ''}`}
                  </td>
                </tr>
              ))}</tbody>
            </table>
          </div>
        )}
      </div>
      <div className="modal-buttons np-buttons">
        {!done && <button type="button" className="btn primary" disabled={busy || !count} onClick={() => void run()}>Import{count ? ` ${count}` : ''}</button>}
        <button type="button" className="btn" onClick={close}>{done ? 'Close' : 'Cancel'}</button>
      </div>
    </Modal>
  );
}

const KINDS: [ExportKind, string][] = [
  ['printer_bundle', 'Printer config bundle(.orca_printer)'], ['filament_bundle', 'Filament bundle(.orca_filament)'],
  ['machine', 'Printer presets(.zip)'], ['filament', 'Filament presets(.zip)'], ['process', 'Process presets(.zip)'],
];

export function ExportDialog() {
  const userPresets = useStore((s) => s.userPresets);
  const machines = useStore((s) => s.machines);
  const [kind, setKind] = useState<ExportKind>('printer_bundle');
  const [picked, setPicked] = useState<Set<string>>(new Set());
  const s = getState();
  const asPreset = (name: string): UserPreset => userPresets.find((u) => u.type === 'machine' && u.name === name)
    ?? { name, type: 'machine', inherits: name, overrides: {} };
  const choices: string[] = kind === 'printer_bundle'
    ? [...userPresets.filter((u) => u.type === 'machine').map((u) => u.name),
      ...machines.filter((m) => userPresets.some((u) => u.type !== 'machine' && printerHas(m.name, u))).map((m) => m.name)]
    : kind === 'filament_bundle' ? [...new Set(userPresets.filter((u) => u.type === 'filament').map((u) => filamentAlias(u.name)))]
      : userPresets.filter((u) => u.type === kind).map((u) => u.name);
  function printerHas(printer: string, u: UserPreset) {
    return getState()[u.type === 'process' ? 'processes' : 'filaments'].some((p) => p.name === u.inherits && (!p.compatible_printers?.length || p.compatible_printers.includes(printer)));
  }
  const selected = choices.filter((c) => picked.has(c));
  const run = () => {
    if (kind === 'printer_bundle') for (const name of selected) {
      const printer = asPreset(name);
      downloadArchive(printerBundle(s, printer));
    }
    else if (kind === 'filament_bundle') for (const name of selected)
      downloadArchive(filamentBundle(s, name, userPresets.filter((u) => u.type === 'filament' && filamentAlias(u.name) === name)));
    else downloadArchive(presetZip(userPresets.filter((u) => u.type === kind && picked.has(u.name)), kind));
    toast(`Exported ${selected.length} ${kind.endsWith('bundle') ? 'bundle' : 'preset'}${selected.length === 1 ? '' : 's'}`);
    close();
  };
  return (
    <Modal className="np-dialog small export-dialog" label="Export Preset Bundle" onClose={close}>
      <ModalTitle title="Export Preset Bundle" onClose={close} />
      <div className="np-dialog-body">
        <fieldset className="export-kinds">
          {KINDS.map(([k, label]) => (
            <label key={k} className="row"><input type="radio" name="export-kind" checked={kind === k} data-autofocus={kind === k ? '' : undefined}
              onChange={() => { setKind(k); setPicked(new Set()); }} />{label}</label>
          ))}
        </fieldset>
        <div className="export-list" role="group" aria-label="Presets to export">
          {choices.length === 0 && <div className="hint-text">No user presets</div>}
          {choices.length > 0 && <label className="row"><Check checked={selected.length === choices.length}
            onCheckedChange={(next) => setPicked(new Set(next ? choices : []))} /><b>Select all</b></label>}
          {choices.map((c) => (
            <label key={c} className="row"><Check checked={picked.has(c)}
              onCheckedChange={(checked) => { const next = new Set(picked); if (checked) next.add(c); else next.delete(c); setPicked(next); }} />{c}</label>
          ))}
        </div>
      </div>
      <div className="modal-buttons np-buttons">
        <button type="button" className="btn primary" disabled={!selected.length} onClick={run}>Export</button>
        <button type="button" className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}
