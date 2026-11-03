import { Dropdown } from './Dropdown';
import { useState } from 'react';
import { getState, useStore } from '../state/store';
import { simplifyModel, triangleCount } from '../plater/meshOps';
import { Modal, ModalTitle } from './Modal';

const LEVELS: [string, number][] = [['Extra high', 0.001], ['High', 0.01], ['Medium', 0.1], ['Low', 0.5], ['Extra low', 1]];

export default function SimplifyDialog({ close }: { close(): void }) {
  const s = getState(), o = s.objects.find((x) => x.id === s.selected?.object);
  const levels = useStore((st) => !!st.features.mesh_simplify_error);
  const [useCount, setUseCount] = useState(!levels);
  const [level, setLevel] = useState(2);
  const [ratio, setRatio] = useState(50);
  if (!o) return null;
  const total = triangleCount(o), wanted = Math.max(1, Math.round(total * (1 - ratio / 100)));
  const count = useCount || !levels;
  return (
    <Modal className="small" label="Simplify" onClose={close}>
      <ModalTitle title="Simplify" onClose={close} />
      <div className="prefs-body">
        <div className="prefs-row"><span>Mesh name</span><span>{o.name}</span></div>
        <div className="prefs-row"><span>Triangles</span><span>{total.toLocaleString()}</span></div>
        {levels && (
          <label className="prefs-row"><span><input type="radio" name="simplify-mode" checked={!count} onChange={() => setUseCount(false)} /> Detail level</span>
            <Dropdown className="combo" value={level} disabled={count} aria-label="Detail level" onValueChange={(next) => setLevel(Number(next))} options={[...LEVELS.map(([label], i) => ({ value: i, label }))]} />
          </label>
        )}
        <label className="prefs-row"><span>{levels && <input type="radio" name="simplify-mode" checked={count} onChange={() => setUseCount(true)} />} Decimate ratio</span>
          <input type="range" min={0} max={99} step={1} value={ratio} disabled={!count} aria-label="Decimate ratio" onChange={(e) => setRatio(Number(e.target.value))} />
          <input className="text num" type="number" min={0} max={99} step={0.5} value={ratio} disabled={!count} onChange={(e) => setRatio(Math.min(99, Math.max(0, Number(e.target.value) || 0)))} /><span>%</span>
        </label>
        {count && <div className="prefs-row"><span /><span>{wanted.toLocaleString()} triangles</span></div>}
      </div>
      <div className="modal-buttons">
        <button className="btn primary" disabled={count && ratio === 0} onClick={() => { close(); void simplifyModel(o.id, wanted, count ? undefined : LEVELS[level][1]); }}>Apply</button>
        <button className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}
