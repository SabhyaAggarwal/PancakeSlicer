import { Dropdown } from './Dropdown';
import { useMemo, useState } from 'react';
import { compatible, getState, setState } from '../state/store';
import { Modal, ModalTitle } from './Modal';

const first = (v: unknown) => String([v].flat()[0] ?? '');

export default function FilamentsDialog({ close }: { close(): void }) {
  const all = useMemo(() => compatible(getState(), 'filament').map((p) => ({ name: p.name, vendor: first(p.config.filament_vendor) || 'Generic', type: first(p.config.filament_type) || 'Other' }))
    .sort((a, b) => (a.name < b.name ? -1 : 1)), []);
  const [chosen, setChosen] = useState<Set<string>>(() => new Set(getState().filamentsToUse ?? all.map((p) => p.name)));
  const [vendor, setVendor] = useState('');
  const [type, setType] = useState('');
  const vendors = [...new Set(all.map((p) => p.vendor))].sort(), types = [...new Set(all.map((p) => p.type))].sort();
  const shown = all.filter((p) => (!vendor || p.vendor === vendor) && (!type || p.type === type));
  const mark = (names: string[], on: boolean) => setChosen((c) => { const n = new Set(c); names.forEach((x) => (on ? n.add(x) : n.delete(x))); return n; });
  const count = all.filter((p) => chosen.has(p.name)).length;
  const ok = () => {
    const before = getState().filamentsToUse, names = new Set(all.map((p) => p.name));
    const value = !before && count === all.length ? null : [...(before ?? []).filter((n) => !names.has(n)), ...all.filter((p) => chosen.has(p.name)).map((p) => p.name)];
    try { localStorage.setItem('orca.web.filamentsToUse', JSON.stringify(value)); } catch {  }
    setState({ filamentsToUse: value });
    close();
  };
  return (
    <Modal label="Filaments" onClose={close} className="small filaments-dialog">
      <ModalTitle title="Filaments" onClose={close} />
      <div className="prefs-body">
        <label className="prefs-row"><span>Vendor</span>
          <Dropdown className="combo" value={vendor} onValueChange={setVendor} options={[{ value: "", label: 'All' }, ...vendors.map((v) => ({ value: v, label: v }))]} />
        </label>
        <label className="prefs-row"><span>Type</span>
          <Dropdown className="combo" value={type} onValueChange={setType} options={[{ value: "", label: 'All' }, ...types.map((t) => ({ value: t, label: t }))]} />
        </label>
        <div className="cut-joint-buttons">
          <button type="button" className="btn" onClick={() => mark(shown.map((p) => p.name), true)}>All</button>
          <button type="button" className="btn" onClick={() => mark(shown.map((p) => p.name), false)}>None</button>
        </div>
        <div className="filaments-list" style={{ maxHeight: 320, overflowY: 'auto' }}>
          {shown.map((p) => (
            <label key={p.name} className="select-row"><input type="checkbox" checked={chosen.has(p.name)} onChange={(e) => mark([p.name], e.target.checked)} />{p.name}</label>
          ))}
        </div>
      </div>
      <div className="modal-buttons">
        <span>{count} of {all.length}</span>
        <span style={{ flex: 1 }} />
        <button type="button" className="btn" onClick={close}>Cancel</button>
        <button type="button" className="btn primary" disabled={!count} title={count ? undefined : 'Select at least one filament'} onClick={ok}>OK</button>
      </div>
    </Modal>
  );
}
