import { getState, resolvePreset, setState, useStore } from '../state/store';
import { requestPreset } from './PresetCombo';
import { listProfiles } from '../profiles/pickerSource';
import { profileKey } from '../profiles/identity';
import { currentEntry, NOT_COMPATIBLE, PickerSkeleton, pickOrOpen, presets, sameStateGroups, usePickerPending } from '../profiles/Pickers';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);


  const groups = (): PickerGroup[] => {
    const state = getState();
    let entries = listProfiles('process');
    const hidden = currentEntry('process', process, entries);
    if (hidden) entries = [...entries, hidden];
    const parentOf = new Map(state.userPresets.map((u) => [profileKey(u), u.parentId as string | undefined]));
    const keys = new Set(entries.map((e) => e.key));
    const parent = (key: string) => { const id = parentOf.get(key); return id && keys.has(id) ? id : undefined; };
    const byName = (a: { name: string }, b: { name: string }) => a.name.localeCompare(b.name);
    const ordered = entries.filter((e) => !parent(e.key)).sort(byName).flatMap((root) => [root, ...entries.filter((c) => parent(c.key) === root.key).sort(byName)]);
    const items: PickerItem[] = ordered.map((p) => ({ id: p.key, label: p.name, child: !!parent(p.key) || undefined, meta: p === hidden ? NOT_COMPATIBLE : undefined, search: `${p.name} ${p.vendor}`, disabled: p.reason }));
    return [{ id: 'profiles', title: '', items } as PickerGroup];
  };
  const pick = (id: string) => pickOrOpen('process', id, k => requestPreset('process', 0, k));
  const sel = resolvePreset(getState(), 'process', process), pending = usePickerPending(!!sel.name);
  return <div className="process-preset-picker"><Picker label="Print settings" value={process} sections groups={sameStateGroups('process', groups)} memory="orca.parity.process" onPick={pick} footer={[{ label: 'New profile', run: () => setState({ presetUx: { kind: 'presets', type: 'process', add: true } }) }, presets('process', process)]}>{pending ? <PickerSkeleton/> : <span className="pk-name">{sel.name ?? 'Choose print settings'}</span>}</Picker></div>;
}
