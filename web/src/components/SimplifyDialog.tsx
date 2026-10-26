import { useState } from 'react';
import { getState } from '../state/store';
import { simplifyModel, triangleCount } from '../plater/meshOps';
import { Modal, ModalTitle } from './Modal';

export default function SimplifyDialog({ close }: { close(): void }) {
  const s = getState(), o = s.objects.find((x) => x.id === s.selected?.object);
  const [ratio, setRatio] = useState(50);
  if (!o) return null;
  const total = triangleCount(o), wanted = Math.max(1, Math.round(total * (1 - ratio / 100)));
  return (
    <Modal className="small" label="Simplify" onClose={close}>
      <ModalTitle title="Simplify" onClose={close} />
      <div className="prefs-body">
        <div className="prefs-row"><span>Mesh name</span><span>{o.name}</span></div>
        <div className="prefs-row"><span>Triangles</span><span>{total.toLocaleString()}</span></div>
        <label className="prefs-row"><span>Decimate ratio</span>
          <input type="range" min={0} max={99} step={1} value={ratio} aria-label="Decimate ratio" onChange={(e) => setRatio(Number(e.target.value))} />
          <input className="text num" type="number" min={0} max={99} step={0.5} value={ratio} onChange={(e) => setRatio(Math.min(99, Math.max(0, Number(e.target.value) || 0)))} /><span>%</span>
        </label>
        <div className="prefs-row"><span /><span>{wanted.toLocaleString()} triangles</span></div>
      </div>
      <div className="modal-buttons">
        <button className="btn primary" disabled={ratio === 0} onClick={() => { close(); void simplifyModel(o.id, wanted); }}>Apply</button>
        <button className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}
