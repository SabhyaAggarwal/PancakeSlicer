import { useState } from 'react';
import { useStore } from '../state/store';
import { meshBoolean, type BooleanOp, type PartKey } from '../plater/meshBoolean';

const OPS: [BooleanOp, string][] = [['union', 'Union'], ['difference', 'Difference'], ['intersection', 'Intersection']];

export default function BooleanPanel() {
  const o = useStore((s) => s.objects.find((x) => x.id === s.selected?.object));
  const busy = useStore((s) => s.loading === 'Mesh Boolean');
  const [op, setOp] = useState<BooleanOp>('union');
  const [del, setDel] = useState<Record<BooleanOp, boolean>>({ union: true, difference: false, intersection: false });
  const parts: [PartKey, string][] = o ? [['main', o.name], ...(o.volumes ?? []).map((v) => [v.id, v.name] as [PartKey, string])] : [];
  const [a, setA] = useState<PartKey>('main');
  const [b, setB] = useState<PartKey>(parts[1]?.[0] ?? 'main');
  if (!o) return null;
  const has = (k: PartKey) => parts.some(([p]) => p === k);
  const first = has(a) ? a : 'main';
  const second = has(b) && b !== first ? b : parts.find(([k]) => k !== first)?.[0];
  const pick = (label: string, value: PartKey, set: (k: PartKey) => void) => (
    <label className="prefs-row cut-joint-row"><span>{label}</span>
      <select className="combo" value={String(value)} aria-label={label} onChange={(e) => set(e.target.value === 'main' ? 'main' : Number(e.target.value))}>
        {parts.map(([k, name]) => <option key={String(k)} value={String(k)}>{name}</option>)}
      </select>
    </label>
  );
  const ready = second !== undefined && !busy;
  const label = OPS.find(([k]) => k === op)![1];
  return (
    <div className="manip-panel brim-ears">
      <div className="manip-title">Mesh Boolean</div>
      <div className="cut-joint-buttons" role="tablist">
        {OPS.map(([k, name]) => <button key={k} type="button" role="tab" aria-selected={op === k} className={`btn ${op === k ? 'primary' : ''}`} onClick={() => setOp(k)}>{name}</button>)}
      </div>
      {pick(op === 'difference' ? 'Subtract from' : 'Part 1', first, setA)}
      {second !== undefined && pick(op === 'difference' ? 'Subtract with' : 'Part 2', second, setB)}
      {op !== 'union' && (
        <label className="prefs-row cut-joint-row"><span>Delete input</span>
          <input type="checkbox" checked={del[op]} onChange={(e) => setDel({ ...del, [op]: e.target.checked })} />
        </label>
      )}
      <div className="cut-joint-buttons"><span style={{ flex: 1 }} />
        <button type="button" className="btn primary" disabled={!ready} onClick={() => void meshBoolean(o.id, op, first, second!, del[op])}>{label}</button>
      </div>
    </div>
  );
}
