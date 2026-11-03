import { getState, resolvePreset, useStore } from '../state/store';
import { requestPreset } from './PresetCombo';
import { createProfileFromBase, listCatalog, listProfiles } from '../profiles/pickerSource';
import { modelLabel, processMode, processTitle } from '../profiles/metadata';
import { presets } from '../profiles/Pickers';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);
  const groups = (catalog = false): PickerGroup[] => {
    const s = getState(), entries = catalog ? listCatalog('process') : listProfiles('process');
    const modes = new Map<string, number>();
    for (const p of entries) modes.set(processMode(p.config), (modes.get(processMode(p.config)) ?? 0) + 1);
    const usual = [...modes].sort((a, b) => b[1] - a[1])[0]?.[0];
    const byModel = new Map<string, PickerItem[]>();
    for (const p of entries) {
      const height = Number(p.config.layer_height) || 0, mode = processMode(p.config);
      const item: PickerItem & {height: number} = {id: p.key, label: processTitle(p.name, p.config, true), meta: [height ? `${height.toFixed(2)} mm` : '', mode !== usual ? mode : ''].filter(Boolean).join(' · '), search: `${p.name} ${p.vendor}`, disabled: p.reason, height};
      const models = new Set((resolvePreset(s, 'process', p.key).base?.compatible_printers ?? []).flatMap(name => {const m = s.machines.find(x => x.name === name);return m ? [modelLabel(m.config, m.vendor, m.model)] : [];}).filter(Boolean));
      for (const model of models.size ? models : ['Any printer']) byModel.set(model, [...byModel.get(model) ?? [], item]);
    }
    return [...byModel].sort(([a], [b]) => a === 'Any printer' ? 1 : b === 'Any printer' ? -1 : a.localeCompare(b))
      .map(([model, items]) => ({id: model, title: model, items: (items as (PickerItem & {height: number})[]).sort((a, b) => a.height - b.height || a.label.localeCompare(b.label))}));
  };
  return <div className="process-preset-picker"><Picker label="Print settings" value={process} groups={() => groups()} memory="orca.parity.process" onPick={name => requestPreset('process', 0, name)} catalog={{label: 'Add print settings', groups: () => groups(true), onAdd: name => createProfileFromBase('process', name)}} footer={[presets('process', process)]}><span className="pk-name">{resolvePreset(getState(),'process',process).name ?? 'Choose print settings'}</span></Picker></div>;
}
