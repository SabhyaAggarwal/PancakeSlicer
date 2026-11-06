


import { Dropdown } from './Dropdown';
import { useState } from 'react';
import { markStale, setState, type PlateObject } from '../state/store';
import { undoable } from '../state/history';
import { EXTRUDERS_LIMIT, type PaintNode, type Painting } from '../plater/paint';

const remapNode = (n: PaintNode, map: number[]): PaintNode =>
  Array.isArray(n) ? n.map((c) => remapNode(c, map)) : n >= 1 && n <= map.length ? map[n - 1] : n;

export function remapObject(o: PlateObject, map: number[]): PlateObject {
  const paint = <T extends { paint?: PlateObject['paint'] }>(x: T): T => {
    const p = x.paint?.color;
    return p ? { ...x, paint: { ...x.paint, color: new Map([...p].map(([t, n]) => [t, remapNode(n, map)])) as Painting } } : x;
  };
  const ext = (e: number) => (e >= 1 && e <= map.length ? map[e - 1] : e);

  return { ...paint(o), extruder: ext(o.extruder), volumes: o.volumes?.map((v) => (v.type === 'part' ? { ...paint(v), extruder: v.extruder ? ext(v.extruder) : 0 } : v)) };
}

export default function RemapFilaments({ object, colours }: { object: PlateObject; colours: string[] }) {
  const n = Math.min(colours.length, EXTRUDERS_LIMIT);
  const identity = Array.from({ length: n }, (_, i) => i + 1);
  const [map, setMap] = useState(identity);
  const current = map.length === n ? map : identity;
  const changed = current.some((d, i) => d !== i + 1);
  const apply = () => {
    undoable('Remap filament assignments', () => setState((s) => ({
      objects: s.objects.map((o) => (o.id === object.id ? remapObject(o, current) : o)), job: markStale(s.job),
    })));
    setMap(identity);
  };
  return (
    <details className="remap-filaments">
      <summary>Remap filaments</summary>
      <div className="remap-grid">
        {current.map((dst, i) => (
          <label key={i} className="remap-row" title={dst !== i + 1 ? `${i + 1} >> ${dst}` : undefined}>
            <span className="paint-filament" style={{ background: colours[i] }}>{i + 1}</span>
            <span>To:</span>
            <Dropdown value={dst} aria-label={`Filament ${i + 1} to`} onValueChange={(next) => setMap(current.map((d, k) => (k === i ? Number(next) : d)))} options={[...identity.map((f) => ({ value: f, label: f }))]} />
            <span className="paint-filament" style={{ background: colours[dst - 1] }}>{dst}</span>
          </label>
        ))}
      </div>
      <div style={{ display: 'flex', gap: 8, marginTop: 4 }}>
        <button className="btn" disabled={!changed} onClick={apply}>Remap</button>
        {changed && <button className="btn" onClick={() => setMap(identity)}>Reset</button>}
      </div>
    </details>
  );
}
