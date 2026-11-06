import { savePreferences } from '../saved-preferences';
import { Check } from './Check';


import { Dropdown } from './Dropdown';
import { useMemo, useState } from 'react';
import { compatible, getState, setState } from '../state/store';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';

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
    savePreferences({'orca.web.filamentsToUse': value});
    setState({ filamentsToUse: value });
    close();
  };
  return (
    <Modal size="m" label="Filaments to show" onClose={close}>
      <ModalTitle title="Filaments to show" onClose={close} />
      <ModalBody>
        <div className="filament-filters">
          <Dropdown className="combo" aria-label="Vendor" value={vendor} onValueChange={setVendor} options={[{ value: "", label: 'All vendors' }, ...vendors.map((v) => ({ value: v, label: v }))]} />
          <Dropdown className="combo" aria-label="Type" value={type} onValueChange={setType} options={[{ value: "", label: 'All types' }, ...types.map((t) => ({ value: t, label: t }))]} />
        </div>
        <div className="modal-pick">
          <div className="modal-pick-head">
            <button type="button" className="btn-ghost" onClick={() => mark(shown.map((p) => p.name), true)}>Select all</button>
            <button type="button" className="btn-ghost" onClick={() => mark(shown.map((p) => p.name), false)}>Clear</button>
          </div>
          <div className="modal-pick-list">{shown.map((p) => (
            <label key={p.name} className="modal-check"><Check checked={chosen.has(p.name)} onCheckedChange={(next) => mark([p.name], next)} /><span data-full-text={p.name}>{p.name}</span></label>
          ))}</div>
        </div>
      </ModalBody>
      <ModalFooter lead={`${count} of ${all.length}`}>
        <button type="button" className="btn" onClick={close}>Cancel</button>
        <button type="button" className="btn-primary" disabled={!count} onClick={ok}>Save</button>
      </ModalFooter>
    </Modal>
  );
}
