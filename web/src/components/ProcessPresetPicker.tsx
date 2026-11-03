import { getState, resolvePreset, useStore } from '../state/store';
import { requestPreset } from './PresetCombo';
import { createProfileFromBase, listCatalog, listProfiles } from '../profiles/pickerSource';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);
  const groups = (catalog = false): PickerGroup[] => {
    const grouped = new Map<number, PickerItem[]>();
    for (const p of catalog ? listCatalog('process') : listProfiles('process')) {
      const height = Number(p.config.layer_height) || 0;
      const reason = p.reason;
      grouped.set(height, [...grouped.get(height) ?? [], {id: p.key, label: p.name, search: `${p.name} ${p.vendor}`, disabled: reason}]);
    }
    return [...grouped].sort((a,b) => Math.abs(a[0] - .2) - Math.abs(b[0] - .2)).map(([h,items]) => ({id: String(h), title: `${h.toFixed(2)} mm`, items}));
  };
  return <div className="process-preset-picker"><Picker label="Process profile" value={process} groups={() => groups()} memory="orca.parity.process" onPick={name => requestPreset('process', 0, name)} catalog={{label: 'Add process', groups: () => groups(true), onAdd: name => createProfileFromBase('process', name)}}><span className="pk-name">{resolvePreset(getState(),'process',process).name ?? 'Choose print settings'}</span></Picker></div>;
}
