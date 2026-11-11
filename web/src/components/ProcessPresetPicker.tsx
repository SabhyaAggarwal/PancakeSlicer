import { useRef, useState } from 'react';
import { getState, resolvePreset, setState, useStore } from '../state/store';
import { AddPrintMenu } from '../profiles/AddPrintMenu';
import { requestPreset } from './PresetCombo';
import { listProfiles } from '../profiles/pickerSource';
import { currentEntry, NOT_COMPATIBLE, PickerSkeleton, pickOrOpen, presets, sameStateGroups, usePickerPending } from '../profiles/Pickers';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);
  const groups = (): PickerGroup[] => {
    let entries = listProfiles('process');
    const hidden = currentEntry('process', process, entries);
    if (hidden) entries = [...entries, hidden];
    const ordered = [...entries].sort((a, b) => a.name.localeCompare(b.name));
    const items: PickerItem[] = ordered.map((p) => ({ id: p.key, label: p.name, meta: p === hidden ? NOT_COMPATIBLE : undefined, search: `${p.name} ${p.vendor}`, disabled: p.reason }));
    return [{ id: 'profiles', title: '', items } as PickerGroup];
  };
  const pick = (id: string) => pickOrOpen('process', id, k => requestPreset('process', 0, k));
  const sel = resolvePreset(getState(), 'process', process), pending = usePickerPending(!!sel.name);
  const [adding, setAdding] = useState(false), trigger = useRef<HTMLButtonElement>(null);
  return <AddPrintMenu anchor={trigger} open={adding} onOpenChange={setAdding} onOpen={(key) => requestPreset('process', 0, key)}
    onCreated={(key) => { requestPreset('process', 0, key); setState({ presetUx: { kind: 'presets', type: 'process', name: key, edit: true } }); }}>
    <div className="process-preset-picker"><Picker label="Print settings" value={process} sections groups={sameStateGroups('process', groups)} memory="orca.parity.process" onPick={pick} triggerProps={{ ref: trigger }} footer={[{ label: 'Add', run: () => requestAnimationFrame(() => setAdding(true)) }, presets('process', process)]}>{pending ? <PickerSkeleton/> : <span className="pk-name">{sel.name ?? 'Choose print settings'}</span>}</Picker></div>
  </AddPrintMenu>;
}
