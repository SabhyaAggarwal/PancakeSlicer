import { useSyncExternalStore } from 'react';
import { distanceComponents, featureText, fmt, measure, measureRows, transformFeature } from '../plater/measure';
import { measureState, setMeasure, subscribeMeasure } from '../plater/measureState';
import { instanceMatrix } from '../plater/geometry';
import { getState, updateObject } from '../state/store';
import { promptDialog } from '../actions';
import { IconButton } from './Icon';

async function editToScale(current: number) {
  const text = await promptDialog('Edit to scale', fmt(current));
  const value = Number(text);
  const s = getState(), sel = s.selected, o = s.objects.find((x) => x.id === sel?.object);
  const inst = o?.instances.find((i) => i.id === sel?.instance);
  if (!o || !inst || sel?.all || !(value > 0) || Math.abs(value - current) < 1e-9 || current <= 0) return;
  const { first, second } = measureState();
  const ratio = value / current, before = instanceMatrix(o, inst);
  updateObject(o.id, { scale: o.scale.map((k) => k * ratio) as [number, number, number] }, 'Scale');
  const after = getState().objects.find((x) => x.id === o.id)!;
  const m = instanceMatrix(after, after.instances.find((i) => i.id === inst.id)!).multiply(before.invert());
  setMeasure({ first: transformFeature(first, m, ratio), second: transformFeature(second, m, ratio) });
}

const FIRST = '#40bfbf', SECOND = '#bf40bf', ORCA = '#009688', AXES = ['#ff3c5b', '#64c818', '#2f88e9'];

export default function MeasurePanel() {
  const { first, second } = useSyncExternalStore(subscribeMeasure, measureState);
  const m = first && second ? measure(first, second) : null;
  const xyz = m && distanceComponents(m);
  const copy = (label: string, value: string) => void navigator.clipboard?.writeText(`${label}: ${value}`);
  return (
    <div className="manip-panel">
      <div className="manip-title">Measure</div>
      <div className="manip-grid" style={{ gridTemplateColumns: 'auto 1fr auto' }}>
        <span style={{ color: FIRST }}>Selection 1</span>
        <span style={{ color: FIRST }}>{featureText(first)}</span>
        {first ? <IconButton icon="revert_btn" size={14} title="Reset" onClick={() => setMeasure({ ...measureState(), first: second, second: undefined })} /> : <span />}
        <span style={{ color: SECOND }}>Selection 2</span>
        <span style={{ color: SECOND }}>{featureText(second)}</span>
        {first && second ? <IconButton icon="revert_btn" size={14} title="Reset" onClick={() => setMeasure({ ...measureState(), second: undefined })} /> : <span />}
        {m && measureRows(m).map(([label, value]) => [
          <span key={`${label}l`} style={{ color: ORCA }}>{label}</span>,
          <span key={`${label}v`}>{value}</span>,
          <span key={`${label}c`} style={{ display: 'flex', gap: 2 }}>
            {/^(Distance|Perpendicular distance|Direct distance)$/.test(label) && getState().selected && !getState().selected?.all && (
              <IconButton icon="edit" size={14} title="Edit to scale" onClick={() => void editToScale(parseFloat(value))} />
            )}
            <IconButton icon="copy_menu" size={14} title="Copy to clipboard" onClick={() => copy(label, value)} />
          </span>,
        ])}
        {xyz && (['X', 'Y', 'Z'] as const).map((a, k) => [
          <span key={`${a}l`} style={{ color: AXES[k] }}>{a}:</span>,
          <input key={`${a}v`} className="text num manip-num" aria-label={`Distance ${a}`} value={xyz.getComponent(k).toFixed(2)} disabled readOnly />,
          <span key={`${a}c`} />,
        ])}
      </div>
    </div>
  );
}
