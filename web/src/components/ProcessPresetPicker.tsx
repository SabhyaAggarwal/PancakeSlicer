import { getState, resolvePreset, useStore } from '../state/store';
import { requestPreset } from './PresetCombo';
import { createProfileFromBase, listCatalog, listProfiles } from '../profiles/pickerSource';
import { processMode, processTitle } from '../profiles/metadata';
import { presets } from '../profiles/Pickers';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);
  const groups = (catalog = false): PickerGroup[] => {
    const entries = catalog ? listCatalog('process') : listProfiles('process');
    const modes = new Map<string, number>();
    for (const p of entries) modes.set(processMode(p.config), (modes.get(processMode(p.config)) ?? 0) + 1);
    const usual = [...modes].sort((a, b) => b[1] - a[1])[0]?.[0];
    const byHeight = new Map<number, PickerItem[]>();
    for (const p of entries) {
      const height = Number(p.config.layer_height) || 0, mode = processMode(p.config);
      byHeight.set(height, [...byHeight.get(height) ?? [], {id: p.key, label: processTitle(p.name, p.config, true), meta: mode !== usual ? mode : undefined, search: `${p.name} ${p.vendor}`, disabled: p.reason}]);
    }
    return [...byHeight].sort(([a], [b]) => (a || Infinity) - (b || Infinity))
      .map(([height, items]) => ({id: `h:${height}`, title: height ? `${height.toFixed(2)} mm` : 'Other', items: items.sort((a, b) => a.label.localeCompare(b.label))}));
  };
  return <div className="process-preset-picker"><Picker label="Print settings" value={process} sections groups={() => groups()} memory="orca.parity.process" onPick={name => requestPreset('process', 0, name)} catalog={{label: 'Add print settings', groups: () => groups(true), onAdd: name => createProfileFromBase('process', name)}} footer={[presets('process', process)]}><span className="pk-name">{resolvePreset(getState(),'process',process).name ?? 'Choose print settings'}</span></Picker></div>;
}
