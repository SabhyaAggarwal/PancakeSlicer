import { useEffect, useState } from 'react';
import { loadHead, slopeMap, type Head } from '../heads';
import { Modal, ModalTitle } from './Modal';

const fmt = (n: number) => String(Number(n.toFixed(3)));

function SlopePlot({ head, gap }: { head: Head; gap: number }) {
  const size = 300, c = size / 2, radius = c - 30;
  const slopes = slopeMap(head, 72, gap);
  const points = slopes.map((s, i) => {
    const phi = (2 * Math.PI * i) / slopes.length, r = (radius * s) / (Math.PI / 2);
    return `${(c + r * Math.cos(phi)).toFixed(1)},${(c - r * Math.sin(phi)).toFixed(1)}`;
  }).join(' ');
  const min = (Math.min(...slopes) * 180) / Math.PI;
  return (
    <figure className="head-plot">
      <svg viewBox={`0 0 ${size} ${size}`} role="img" aria-label="Clean slope limit by direction">
        {[30, 60, 90].map((a) => <g key={a}><circle cx={c} cy={c} r={(radius * a) / 90} fill="none" className="plot-grid" />
          <text x={c + (radius * a) / 90 + 2} y={c - 2} fontSize="11" fill="currentColor">{a}°</text></g>)}
        <path d={`M ${c - radius} ${c} H ${c + radius} M ${c} ${c - radius} V ${c + radius}`} className="plot-grid" />
        <text x={c + radius - 4} y={c - 16} fontSize="11" fill="currentColor">+X</text>
        <text x={c - 8} y={c - radius - 6} fontSize="11" fill="currentColor">+Y</text>
        <polygon points={points} fill="rgba(0, 150, 136, 0.18)" stroke="#009688" strokeWidth="1.5" />
      </svg>
      <figcaption>Min {min.toFixed(1)}°</figcaption>
    </figure>
  );
}

export default function HeadDialog({ id, layerHeight, close }: { id: string; layerHeight: number; close: () => void }) {
  const [head, setHead] = useState<Head | null | undefined>(undefined);
  const [gap, setGap] = useState(layerHeight > 0 ? layerHeight : 0.2);
  useEffect(() => void loadHead(id || 'generic-conservative').then(setHead), [id]);
  return (
    <Modal className="np-dialog head-dialog" label="Non-planar print head" onClose={close}>
      <ModalTitle title="Non-planar print head" onClose={close} />
      <div className="np-dialog-body">
        {head === undefined && <div role="status">Loading head…</div>}
        {head === null && <div role="alert">"{id}" not in head library: generic conservative head</div>}
        {head && <>
          <dl className="head-identity">
            <dt>Head ID</dt><dd>{head.id} {head.calibrated ? '(calibrated)' : '(uncalibrated)'}</dd>
            <dt>Name</dt><dd>{head.name}</dd>
            <dt>Vertical margin</dt><dd>{fmt(head.margin)} mm</dd>
            <dt>Lateral margin</dt><dd>{fmt(head.lateral_margin)} mm</dd>
            <dt><label htmlFor="head-gap">Layer height</label></dt>
            <dd><input id="head-gap" className="text num" type="number" min="0.01" max="2" step="0.01" value={gap} onChange={(e) => e.target.valueAsNumber > 0 && setGap(e.target.valueAsNumber)} /> mm</dd>
          </dl>
          <div className="head-body">
            <div className="head-boxes">
              <table className="np-table">
                <thead><tr>{['Component', 'Z min (mm)', 'Z max (mm)', 'Center X (mm)', 'Center Y (mm)', 'Half X (mm)', 'Half Y (mm)', 'Infinite X', 'Infinite Y', 'Nozzle'].map((h) => <th key={h}>{h}</th>)}</tr></thead>
                <tbody>{head.boxes.map((b, i) => <tr key={i}>
                  <td>{b.name ?? ''}</td><td>{fmt(b.min[2])}</td><td>{fmt(b.max[2])}</td>
                  <td>{fmt((b.min[0] + b.max[0]) / 2)}</td><td>{fmt((b.min[1] + b.max[1]) / 2)}</td>
                  <td>{fmt((b.max[0] - b.min[0]) / 2)}</td><td>{fmt((b.max[1] - b.min[1]) / 2)}</td>
                  <td>{b.infinite_x ? '✓' : ''}</td><td>{b.infinite_y ? '✓' : ''}</td><td>{b.nozzle ? '✓' : ''}</td>
                </tr>)}</tbody>
              </table>
            </div>
            <SlopePlot head={head} gap={gap} />
          </div>
          {head.provenance && <details className="head-provenance"><summary>Provenance</summary><p>{head.provenance}</p></details>}
        </>}
      </div>
      <div className="modal-buttons np-buttons"><button className="btn primary" type="button" data-autofocus onClick={close}>Close</button></div>
    </Modal>
  );
}
