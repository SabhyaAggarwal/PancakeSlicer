import { getState, resolvePreset, setLayerHeight, useStore } from '../state/store';
import { effectiveLayerHeight, printContext, profileChain } from '../profiles/context';
import { availableHeights, chainRecord, sameMm } from '../profiles/dynamic';
import { resolveProfile } from '../profiles/model';
import { requestPreset } from './PresetCombo';
import { createProfileFromBase, listCatalog, listProfiles } from '../profiles/pickerSource';
import { currentEntry, noPrinterGroup, NOT_COMPATIBLE, PickerSkeleton, pickOrOpen, presets, sameStateGroups, usePickerPending } from '../profiles/Pickers';
import { Picker, type PickerGroup, type PickerItem } from './picker/Picker';
import './process-settings.css';
export function ProcessPresetPicker() {
  const process = useStore(s => s.process);
  useStore(s=>s.userPresets); useStore(s=>s.printer); useStore(s=>s.dirty.machine); useStore(s=>s.dirty.process); useStore(s=>s.projectPresets); useStore(s=>s.processes);
  const groups = (catalog = false): PickerGroup[] => {
    let entries = catalog ? listCatalog('process') : listProfiles('process');
    const hidden = catalog ? undefined : currentEntry('process', process, entries);
    if (hidden) entries = [...entries, hidden];
    const s = getState(), ctx = printContext(s), byHeight = new Map<number, PickerItem[]>();
    for (const p of entries) {
      const resolved = catalog ? undefined : resolveProfile(s, 'process', p.key), links = resolved ? profileChain(s, 'process', resolved) : [];
      const own = Number(p.config.layer_height) || 0, offered = links.some(l => l.dynamic) ? availableHeights(chainRecord(links), ctx) : [];
      const at = p.key === process ? effectiveLayerHeight(s) ?? own : own;
      for (const height of offered.length ? offered : [own])
        byHeight.set(height, [...byHeight.get(height) ?? [], {id: sameMm(height, at) ? p.key : `${p.key}\0${height}`, label: p.name, meta: p === hidden ? NOT_COMPATIBLE : undefined, search: `${p.name} ${p.vendor}`, disabled: p.reason}]);
    }
    return [...byHeight].sort(([a], [b]) => (a || Infinity) - (b || Infinity))
      .map(([height, items]) => ({id: `h:${height}`, title: height ? `${height.toFixed(2)} mm` : 'Other', items: items.sort((a, b) => a.label.localeCompare(b.label))}) as PickerGroup)
      .concat(catalog ? [] : noPrinterGroup('process'));
  };
  const pick = (id: string) => { const [key, height] = id.split('\0'); pickOrOpen('process', key, k => { requestPreset('process', 0, k); if (height) setLayerHeight(Number(height)); }); };
  const sel = resolvePreset(getState(), 'process', process), pending = usePickerPending(!!sel.name);
  return <div className="process-preset-picker"><Picker label="Print settings" value={process} sections groups={sameStateGroups('process', () => groups())} memory="orca.parity.process" onPick={pick} catalog={{label: 'Add print settings', groups: sameStateGroups('process-catalog', () => groups(true)), onAdd: name => createProfileFromBase('process', name)}} footer={[presets('process', process)]}>{pending ? <PickerSkeleton/> : <span className="pk-name">{sel.name ?? 'Choose print settings'}</span>}</Picker></div>;
}
