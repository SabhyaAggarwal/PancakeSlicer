import { effectiveConfig, getState, markStale, setState, useStore, type PlateObject, type State } from '../state/store';
import { undoable } from '../state/history';
import { listedPlates } from '../plater/plates';
import { Modal, ModalTitle } from './Modal';

type Col = { key: string; label: string; kind: 'float' | 'int' | 'percent' | 'bool' | 'enum'; choices?: [string, string][] };
const COLS: Col[] = [
  { key: 'layer_height', label: 'Layer height', kind: 'float' },
  { key: 'wall_loops', label: 'Wall loops', kind: 'int' },
  { key: 'sparse_infill_density', label: 'Infill density(%)', kind: 'percent' },
  { key: 'enable_support', label: 'Support', kind: 'bool' },
  { key: 'brim_type', label: 'Brim', kind: 'enum', choices: [['auto_brim', 'Auto'], ['brim_ears', 'Mouse ear'], ['painted', 'Painted'], ['outer_only', 'Outer brim only'],
    ['inner_only', 'Inner brim only'], ['outer_and_inner', 'Outer and inner brim'], ['no_brim', 'No-brim']] },
  { key: 'outer_wall_speed', label: 'Outer wall speed', kind: 'float' },
];

function editObject(id: number, name: string, f: (o: PlateObject) => PlateObject) {
  undoable(name, () => setState((s) => ({
    objects: s.objects.map((o) => (o.id === id ? f(o) : o)),
    projectPlates: s.projectPlates.map((p) => (p.index === s.plateIndex ? p : { ...p, objects: p.objects.map((o) => (o.id === id ? f(o) : o)) })),
    job: markStale(s.job),
  })));
}

const setting = (id: number, key: string, value: string | null) => editObject(id, value === null ? 'Reset object setting' : 'Change object setting', (o) => {
  const settings = { ...o.settings };
  if (value === null) delete settings[key];
  else settings[key] = value;
  return { ...o, settings };
});

function Cell({ s, o, col }: { s: State; o: PlateObject; col: Col }) {
  const own = o.settings[col.key];
  const value = String(own ?? effectiveConfig(s, 'process')[col.key] ?? '');
  const set = (v: string) => setting(o.id, col.key, v);
  const input = col.kind === 'bool'
    ? <input type="checkbox" checked={value === '1' || value === 'true'} aria-label={col.label} onChange={(e) => set(e.target.checked ? '1' : '0')} />
    : col.kind === 'enum'
      ? <select className="combo" value={value} aria-label={col.label} onChange={(e) => set(e.target.value)}>{col.choices!.map(([k, l]) => <option key={k} value={k}>{l}</option>)}</select>
      : <input className="text num" type="number" step={col.kind === 'float' ? 0.01 : 1} defaultValue={parseFloat(value) || 0} key={value} aria-label={col.label}
        onBlur={(e) => { const n = Number(e.target.value); if (Number.isFinite(n) && String(n) !== String(parseFloat(value))) set(col.kind === 'percent' ? `${n}%` : String(n)); }}
        onKeyDown={(e) => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); }} />;
  return (
    <td className={own === undefined ? 'global' : ''}>
      {input}
      {own !== undefined && <button type="button" className="icon-reset" title="Reset to the global value" onClick={() => setting(o.id, col.key, null)}>↺</button>}
    </td>
  );
}

export default function ObjectTable({ close }: { close(): void }) {
  useStore((s) => s.objects);
  useStore((s) => s.projectPlates);
  const s = getState();
  const rows = listedPlates(s).flatMap((p) => p.objects.map((o) => ({ plate: p.index, o })));
  return (
    <Modal label="Object/Part Settings" onClose={close} className="object-table-dialog">
      <ModalTitle title="Object/Part Settings" onClose={close} />
      <div className="object-table-body">
        <table className="object-table">
          <thead><tr><th>Printable</th><th>Plate</th><th>Name</th><th>Filament</th>{COLS.map((c) => <th key={c.key}>{c.label}</th>)}</tr></thead>
          <tbody>
            {rows.map(({ plate, o }) => (
              <tr key={o.id}>
                <td><input type="checkbox" aria-label="Printable" checked={o.instances.every((i) => i.printable !== false)}
                  onChange={(e) => editObject(o.id, 'Change printable', (x) => ({ ...x, instances: x.instances.map((i) => ({ ...i, printable: e.target.checked })) }))} /></td>
                <td>{plate}</td>
                <td className="name" title={o.name}>{o.name}</td>
                <td><select className="combo" value={o.extruder} aria-label="Filament" onChange={(e) => editObject(o.id, 'Change filament', (x) => ({ ...x, extruder: Number(e.target.value) }))}>
                  {s.filamentSlots.map((_, i) => <option key={i} value={i + 1}>{i + 1}</option>)}
                </select></td>
                {COLS.map((c) => <Cell key={c.key} s={s} o={o} col={c} />)}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <div className="modal-buttons"><span style={{ flex: 1 }} /><button type="button" className="btn primary" onClick={close}>OK</button></div>
    </Modal>
  );
}
