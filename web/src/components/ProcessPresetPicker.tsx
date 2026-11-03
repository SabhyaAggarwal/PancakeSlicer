import { getState, resolvePreset, useStore } from '../state/store';
import { requestPreset } from './PresetCombo';
import { createProfileFromBase, listCatalog, listProfiles } from '../profiles/pickerSource';
import { selectCompatibility } from '../profiles/compatibility';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const s = useStore(s => s);
  const groups = (catalog = false): PickerGroup[] => {
    const st = getState(), grouped = new Map<number, PickerItem[]>();
    for (const p of catalog ? listCatalog('process') : listProfiles('process')) {
      const height = Number(resolvePreset(st, 'process', p.name).config.layer_height) || 0;
      const reason = catalog ? p.reason : selectCompatibility(st, 'process', p.name).reason;
      grouped.set(height, [...grouped.get(height) ?? [], {id: p.name, label: p.name, search: `${p.name} ${p.vendor}`, disabled: reason}]);
    }
    return [...grouped].sort((a,b) => Math.abs(a[0] - .2) - Math.abs(b[0] - .2)).map(([h,items]) => ({id: String(h), title: `${h.toFixed(2)} mm`, items}));
  };
  return <div className="process-preset-picker"><Picker label="Process profile" value={s.process} groups={() => groups()} memory="orca.parity.process" onPick={name => requestPreset('process', 0, name)} catalog={{label: 'Add process', groups: () => groups(true), onAdd: name => createProfileFromBase('process', name)}}><span className="pk-name">{s.process || 'Choose process'}</span></Picker></div>;
}
