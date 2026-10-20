import { useSyncExternalStore } from 'react';
import { distanceComponents, featureText, measure, measureRows } from '../plater/measure';
import { measureState, setMeasure, subscribeMeasure } from '../plater/measureState';
import { IconButton } from './Icon';

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
          <IconButton key={`${label}c`} icon="copy_menu" size={14} title="Copy to clipboard" onClick={() => copy(label, value)} />,
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
