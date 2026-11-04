import { useState } from 'react';
import { Choice } from '../components/kit';
import { Dropdown } from '../components/Dropdown';
import { OPTIONS } from '../options';
import { Glyph } from '../components/Glyph';
import { heightsFor, scaledValue, type ScaleBasis, type ScaleRow } from './auto-rules';
import { labelOf } from './PrintOverrides';

const GROUPS: { name: string; basis: ScaleBasis; keys: string[] }[] = [
  { name: 'Line widths', basis: 'nozzle', keys: ['line_width', 'inner_wall_line_width', 'sparse_infill_line_width', 'internal_solid_infill_line_width', 'top_surface_line_width', 'support_line_width'] },
  { name: 'Support Z gap', basis: 'layer', keys: ['support_top_z_distance', 'support_bottom_z_distance'] },
];
const NAMES: Record<string, string> = {
  outer_wall_line_width: 'Outer wall', initial_layer_line_width: 'First layer width', initial_layer_print_height: 'First layer height',
  top_shell_layers: 'Top shell layers', bottom_shell_layers: 'Bottom shell layers', inner_wall_line_width: 'Inner wall width',
  sparse_infill_line_width: 'Sparse infill width', internal_solid_infill_line_width: 'Solid infill width', top_surface_line_width: 'Top surface width',
  support_line_width: 'Support width', support_top_z_distance: 'Support top Z gap', support_bottom_z_distance: 'Support bottom Z gap', line_width: 'Default line width',
};
const nameOf = (key: string) => NAMES[key] ?? OPTIONS[key]?.label ?? key;
const NOZZLES = [0.2, 0.4, 0.6, 0.8];
const BASES: { id: ScaleBasis; label: string }[] = [{ id: 'nozzle', label: '× nozzle' }, { id: 'layer', label: '× layer height' }, { id: 'perLayer', label: '÷ layer height' }];
const mm = (v: number) => String(Math.round(v * 1000) / 1000);
const isWidth = (key: string) => key === 'line_width' || key.endsWith('_line_width');

interface Formula { name: string; keys: string[]; basis: ScaleBasis; factor: number }
function formulasOf(rows: ScaleRow[], split: ReadonlySet<string>): Formula[] {
  const out: Formula[] = [], used = new Set<string>(), named = new Set<string>();
  for (const row of rows) {
    if (used.has(row.key)) continue;
    const group = GROUPS.find((g) => g.basis === row.basis && g.keys.includes(row.key) && !split.has(row.key));
    const members = group ? rows.filter((r) => r.basis === group.basis && group.keys.includes(r.key) && !split.has(r.key) && r.factor === row.factor) : [row];
    members.forEach((m) => used.add(m.key));
    const name = !group || members.length < 2 ? nameOf(row.key) : named.has(group.name) ? members.map((m) => nameOf(m.key)).join(', ') : group.name;
    if (group) named.add(group.name);
    out.push({ name, keys: members.map((m) => m.key), basis: row.basis, factor: row.factor });
  }
  return out;
}

function FormulaLine({ f, readOnly, onFactor, onRemove }: { f: Formula; readOnly?: boolean; onFactor(v: number): void; onRemove(): void }) {
  const input = <input className="text num dyn-factor" type="number" step="0.01" min="0.01" aria-label={`${f.name} factor`} disabled={readOnly} value={f.factor}
    onChange={(e) => { const v = Number(e.target.value); if (v > 0) onFactor(v); }}/>;
  return <li className="dyn-formula">
    <span className="dyn-name">{f.name}</span><span className="dyn-eq">=</span>
    <span className="dyn-expr"><span className="dyn-ceil" data-hidden={f.basis !== 'perLayer' || undefined}>⌈</span>{input}
      {f.basis === 'perLayer' ? 'mm ÷ layer height ⌉' : `× ${f.basis === 'nozzle' ? 'nozzle' : 'layer height'}`}</span>
    <span/>{!readOnly && <button type="button" className="reset-icon row-remove" aria-label="Remove formula" onClick={onRemove}><Glyph name="trash" size={14}/></button>}
  </li>;
}

export function DynamicTab({ rows, nozzle, height, minHeight, maxHeight, readOnly, onChange, fromPrinter }: {
  rows: ScaleRow[]; nozzle: number; height: number; minHeight?: number; maxHeight?: number; readOnly?: boolean; onChange(rows: ScaleRow[]): void;
   fromPrinter?: { name: string; values: Record<string, unknown> };
}) {
  const [split, setSplit] = useState<ReadonlySet<string>>(() => new Set());
  const [adding, setAdding] = useState<{ key: string; basis: ScaleBasis } | null>(null);
  const [folded, setFolded] = useState<ReadonlySet<number>>(() => new Set());
  const formulas = formulasOf(rows, split);
  const setFactor = (keys: string[], factor: number) => onChange(rows.map((r) => (keys.includes(r.key) ? { ...r, factor } : r)));
  const remove = (keys: string[]) => onChange(rows.filter((r) => !keys.includes(r.key)));
  const add = () => {
    if (!adding) return;
    const existing = rows.find((r) => r.key === adding.key);
    setSplit(new Set([...split, adding.key]));
    if (!existing) onChange([...rows, { key: adding.key, basis: adding.basis, factor: 1 }]);
    setAdding(null);
  };
  const candidates = Object.values(OPTIONS).filter((o) => o.label && ['Float', 'FloatOrPercent', 'Int'].includes(o.type) && !formulas.some((f) => f.keys.length === 1 && f.keys[0] === o.key))
    .sort((a, b) => nameOf(a.key).localeCompare(nameOf(b.key)));
  const cell = (f: Formula, n: number, h: number) => {
    const row: ScaleRow = { key: f.keys[0], basis: f.basis, factor: f.factor }, v = scaledValue(row, { nozzle: n, layerHeight: h });
    const raw = f.basis === 'nozzle' ? f.factor * n : f.basis === 'layer' ? f.factor * h : v, clamped = f.basis !== 'perLayer' && Math.abs(raw - v) > 1e-6;
    const why = isWidth(row.key) ? 'Kept within 90–150 % of the nozzle' : 'Kept within 75 % of the nozzle';
    return <td key={f.keys.join()} className="n" {...(clamped ? { 'data-clamped': true, 'data-truncated': 'true', 'data-full-text': why } : {})}>{f.basis === 'perLayer' ? v : mm(v)}</td>;
  };
  return <section className="dyn-tab" aria-label="Dynamic">
    <div className="dyn-rows"><ul className="dyn-formulas">
      {formulas.map((f) => <FormulaLine key={f.keys.join(',')} f={f} readOnly={readOnly} onFactor={(v) => setFactor(f.keys, v)} onRemove={() => remove(f.keys)}/>)}
      {!readOnly && <li className="dyn-formula dyn-add">{adding
        ? <><Dropdown className="combo" aria-label="Setting" value={adding.key} onValueChange={(key) => setAdding({ ...adding, key })} options={candidates.map((o) => ({ value: o.key, label: nameOf(o.key) }))}/>
          <Choice label="Basis" value={adding.basis} options={BASES} onChange={(basis) => setAdding({ ...adding, basis: basis as ScaleBasis })}/>
          <button type="button" className="btn" disabled={!adding.key} onClick={add}>Add</button><button type="button" className="btn-ghost" onClick={() => setAdding(null)}>Cancel</button></>
        : <button type="button" className="btn-ghost" onClick={() => setAdding({ key: '', basis: 'nozzle' })}>+ Add formula</button>}</li>}
    </ul>
    {fromPrinter && Object.keys(fromPrinter.values).length > 0 && <ul className="dyn-formulas dyn-printer" aria-label={`From ${fromPrinter.name}`}>
      <li className="dyn-group">From {fromPrinter.name}</li>
      {Object.entries(fromPrinter.values).map(([k, v]) => <li key={k} className="dyn-formula"><span className="dyn-name">{labelOf(k)}</span><span className="dyn-eq">=</span><span className="dyn-expr"><span className="dyn-ceil" data-hidden>⌈</span>{String(Array.isArray(v) ? v[0] : v)}{OPTIONS[k]?.sidetext ? ` ${OPTIONS[k]?.sidetext}` : ''}</span></li>)}
    </ul>}</div>
    <div className="dyn-table-scroll"><table className="dyn-table">
      <thead><tr><th>Layer</th>{formulas.map((f) => <th key={f.keys.join()} className="n">{f.name}</th>)}</tr></thead>
      {NOZZLES.map((n) => {
        const own = Math.abs(n - nozzle) < 1e-6, heights = heightsFor(n, own ? minHeight : Math.ceil(n * 0.25 / 0.04 - 1e-6) * 0.04, own ? maxHeight : undefined).filter((h) => h <= n * 0.75 + 1e-6), open = !folded.has(n);
        return <tbody key={n}>
          <tr className="dyn-nozzle"><th colSpan={formulas.length + 1}><button type="button" aria-expanded={open} onClick={() => setFolded(open ? new Set([...folded, n]) : new Set([...folded].filter((x) => x !== n)))}>{n} mm nozzle</button></th></tr>
          {open && heights.map((h) => <tr key={h} className={Math.abs(n - nozzle) < 1e-6 && Math.abs(h - height) < 1e-6 ? 'is-current' : undefined}>
            <td className="n">{h.toFixed(2)}</td>{formulas.map((f) => cell(f, n, h))}
          </tr>)}
        </tbody>;
      })}
    </table></div>
  </section>;
}
