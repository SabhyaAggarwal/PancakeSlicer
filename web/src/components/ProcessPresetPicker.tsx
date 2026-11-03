import { getState, resolvePreset, useStore } from '../state/store';
import { requestPreset } from './PresetCombo';
import { createProfileFromBase, listCatalog, listProfiles, type PickerEntry } from '../profiles/pickerSource';
import { processMode, processTitle } from '../profiles/metadata';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);
  const groups = (catalog = false): PickerGroup[] => {
    const grouped = new Map<number, PickerEntry[]>(), entries = catalog ? listCatalog('process') : listProfiles('process');
    const modes = new Map<string, number>();
    for (const p of entries) {
      const height = Number(p.config.layer_height) || 0;
      grouped.set(height, [...grouped.get(height) ?? [], p]);
      modes.set(processMode(p.config), (modes.get(processMode(p.config)) ?? 0) + 1);
    }
    const usual = [...modes].sort((a, b) => b[1] - a[1])[0]?.[0];
    const row = (list: PickerEntry[]) => list.map((p): PickerItem => {
      const title = processTitle(p.name, p.config, !p.reason), clash = list.some(q => q !== p && processTitle(q.name, q.config, !q.reason) === title);
      return {id: p.key, label: clash ? processTitle(p.name, p.config, false) : title, meta: processMode(p.config) !== usual ? processMode(p.config) : undefined, search: `${p.name} ${p.vendor}`, disabled: p.reason};
    });
    return [...grouped].sort((a,b) => Math.abs(a[0] - .2) - Math.abs(b[0] - .2)).map(([h,list]) => ({id: String(h), title: `${h.toFixed(2)} mm`, items: row(list)}));
  };
  return <div className="process-preset-picker"><Picker label="Process profile" value={process} groups={() => groups()} memory="orca.parity.process" onPick={name => requestPreset('process', 0, name)} catalog={{label: 'Add process', groups: () => groups(true), onAdd: name => createProfileFromBase('process', name)}}><span className="pk-name">{resolvePreset(getState(),'process',process).name ?? 'Choose print settings'}</span></Picker></div>;
}
