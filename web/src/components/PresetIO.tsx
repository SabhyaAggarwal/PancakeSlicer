import { Check } from './Check';
import { useRef, useState } from 'react';
import type { PresetType,UserPreset } from '../api/types';
import { applyImportPlans, downloadArchive, filamentAlias, filamentBundle, planImport, presetZip, printerBundle, readPresetFile, type ExportKind, type ImportPlan } from '../preset-io';
import { getState, setState, toast, useStore } from '../state/store';
import { Modal, ModalTitle } from './Modal';
import { createMachine, useMachine } from '../flow/fsm';

const TYPE_NAME: Record<PresetType, string> = { machine: 'Printer', filament: 'Filament', process: 'Process' };
const ACCEPT = '.json,.zip,.bbscfg,.orca_printer,.orca_filament,.orca_bundle';
const close = () => setState({ presetUx: null });

type Row = ImportPlan & { checked: boolean; result?: string; ok?: boolean };
const importable = (r: ImportPlan) => !!(r.preset || r.base || r.parsed.physical);

type ImportState = 'empty' | 'reading' | 'review' | 'importing' | 'imported';
type ImportEvent = { type: 'FILES' } | { type: 'PARSED'; request: number; rows: Row[] } | { type: 'READ_FAIL'; request: number; error: string }
  | { type: 'ROWS'; rows: Row[] } | { type: 'IMPORT' } | { type: 'IMPORTED'; rows: Row[] } | { type: 'IMPORT_FAIL'; rows: Row[] };
interface ImportContext { request: number; rows: Row[] | null; error: string | null }
const files = { target: 'reading', action: (c: ImportContext) => ({ request: c.request + 1, error: null }) } as const;
const createImport = () => createMachine<ImportState, ImportEvent, ImportContext>({
  id: 'preset-import', initial: 'empty', strict: true, context: { request: 0, rows: null, error: null },
  states: {
    empty: { on: { FILES: files } },
    reading: { on: {
      PARSED: { target: 'review', guard: (c, e) => e.request === c.request, action: (_, e) => ({ rows: e.rows }) },
      READ_FAIL: [{ target: 'review', guard: (c, e) => e.request === c.request && !!c.rows, action: (_, e) => ({ error: e.error }) },
        { target: 'empty', guard: (c, e) => e.request === c.request, action: (_, e) => ({ error: e.error }) }],
    } },
    review: { on: { FILES: files, ROWS: { action: (_, e) => ({ rows: e.rows }) },
      IMPORT: { target: 'importing', guard: (c) => !!c.rows?.some((r) => r.checked && importable(r)), action: () => ({ error: null }) } } },
    importing: { on: { IMPORTED: { target: 'imported', action: (_, e) => ({ rows: e.rows }) }, IMPORT_FAIL: { target: 'review', action: (_, e) => ({ rows: e.rows }) } } },
    imported: { on: { FILES: files } },
  },
});

export function ImportDialog() {
  const [machine] = useState(createImport);
  const { value, context: { rows, error } } = useMachine(machine);
  const busy = value === 'reading' || value === 'importing', done = value === 'imported';
  const setRows = (next: Row[]) => machine.send({ type: 'ROWS', rows: next });
  const [drag, setDrag] = useState(false);
  const input = useRef<HTMLInputElement>(null);

  const read = async (picked: FileList | File[]) => {
    const list = [...picked];
    if (!list.length || !machine.can({ type: 'FILES' })) return;
    machine.send({ type: 'FILES' });
    const request = machine.get().context.request;
    try {
      const parsed = (await Promise.all(list.map(readPresetFile))).flat();
      machine.send({ type: 'PARSED', request, rows: planImport(getState(), parsed).map((p) => ({ ...p, checked: p.status !== 'skip' })) });
    } catch (e) { machine.send({ type: 'READ_FAIL', request, error: e instanceof Error ? e.message : String(e) }); }
  };

  const run = async () => {
    if (!rows || !machine.can({ type: 'IMPORT' })) return;
    machine.send({ type: 'IMPORT' });
    const chosen=rows.filter(r=>r.checked && importable(r));
    try {
      const result=await applyImportPlans(chosen);
      machine.send({ type: 'IMPORTED', rows: rows.map(row=>chosen.includes(row) ? {...row,ok:true,result:row.status === 'replace' ? 'replaced' : 'imported'} : row) });
      toast([`Imported ${result.profiles} profiles and ${result.bases} templates`,...(result.connections?[`${result.connections} printer connection${result.connections>1?'s':''}`]:[]),...result.problems].join('\n'));
    }catch(e){machine.send({ type: 'IMPORT_FAIL', rows: rows.map(row=>chosen.includes(row) ? {...row,ok:false,result:e instanceof Error ? e.message : String(e)} : row) });}
  };

  const count = rows?.filter((r) => r.checked && importable(r)).length ?? 0;
  return (
    <Modal className="np-dialog import-dialog" label="Import profiles" onClose={close}>
      <ModalTitle title="Import profiles" onClose={close} />
      <div className={`np-dialog-body import-drop ${drag ? 'drag' : ''}`}
        onDragOver={(e) => { e.preventDefault(); setDrag(true); }} onDragLeave={() => setDrag(false)}
        onDrop={(e) => { e.preventDefault(); e.stopPropagation(); setDrag(false); void read(e.dataTransfer.files); }}>
        <div className="row">
          <button type="button" className="btn primary" data-autofocus onClick={() => input.current?.click()} disabled={busy}>Choose files…</button>
          <input ref={input} type="file" multiple hidden accept={ACCEPT} aria-label="Profile files" onChange={(e) => { if (e.target.files) void read(e.target.files); e.target.value = ''; }} />
        </div>
        {busy && <div role="status">{value === 'importing' ? 'Importing…' : 'Reading…'}</div>}
        {error && <div className="np-error" role="alert">{error}</div>}
        {rows && !rows.length && <div className="hint-text">No profiles found</div>}
        {rows && rows.length > 0 && (
          <div className="diff-scroll">
            <table className="np-table">
              <thead><tr><th><Check aria-label="Select all" disabled={done || busy} checked={rows.every((r) => r.checked || !importable(r))}
                onCheckedChange={(next) => setRows(rows.map((r) => ({ ...r, checked: importable(r) && next })))} /></th>
                <th>Preset</th><th>Type</th><th>Parent</th><th>{done ? 'Result' : 'Action'}</th></tr></thead>
              <tbody>{rows.map((r, i) => (
                <tr key={`${r.parsed.file}:${i}`}>
                  <td><Check aria-label={`Import ${r.parsed.name}`} disabled={!importable(r) || done || busy} checked={r.checked}
                    onCheckedChange={(next) => setRows(rows.map((x, j) => (j === i ? { ...x, checked: next } : x)))} /></td>
                  <td>{r.parsed.name}</td>
                  <td>{r.parsed.type ? TYPE_NAME[r.parsed.type] : r.parsed.physical ? 'Physical printer' : '—'}</td>
                  <td>{r.preset?.inherits ?? r.parsed.inherits ?? ''}</td>
                  <td className={r.ok === false ? 'np-error' : r.status === 'replace' && !done ? 'np-warn-text' : undefined}>
                    {done || r.ok === false ? (r.result ?? (importable(r) ? 'not selected' : `skipped: ${r.note}`))
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
  ['printer_bundle', 'Printer bundle (.orca_printer)'], ['filament_bundle', 'Filament bundle (.orca_filament)'],
  ['machine', 'Printers (.zip)'], ['filament', 'Filaments (.zip)'], ['process', 'Print settings (.zip)'],
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
    <Modal className="np-dialog small export-dialog" label="Export profiles" onClose={close}>
      <ModalTitle title="Export profiles" onClose={close} />
      <div className="np-dialog-body">
        <fieldset className="export-kinds">
          {KINDS.map(([k, label]) => (
            <label key={k} className="row"><input type="radio" name="export-kind" checked={kind === k} data-autofocus={kind === k ? '' : undefined}
              onChange={() => { setKind(k); setPicked(new Set()); }} />{label}</label>
          ))}
        </fieldset>
        <div className="export-list" role="group" aria-label="Profiles to export">
          {choices.length === 0 && <div className="hint-text">No custom profiles</div>}
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
