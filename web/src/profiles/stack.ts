import type { Config, ConfigValue, PresetType } from '../api/types';
import { OPTIONS } from '../options';
import { undoable } from '../state/history';
import { presetValue, sameValue as same, selectChanges, type SettingChange } from '../state/changes';
import { editRange, removeRangeOptions } from '../plater/ranges';
import {
  getState, presetEdits, removeObjectOptions, removeVolumeOptions, resolvePreset, setOption, setState, type State,
} from '../state/store';

export { same };
export { selectChanges as changesOf } from '../state/changes';
export type Change = SettingChange;
export type Layer = 'default' | 'system' | 'mine' | 'project' | 'plate' | 'object' | 'part' | 'range';
export const LAYER_NAME: Record<Layer, string> = {
  default: 'Default', system: 'System', mine: 'Mine', project: 'Project', plate: 'Plate', object: 'Object', part: 'Part', range: 'Range',
};
export interface StackRow {
  layer: Layer;
  name?: string;
  value: ConfigValue;
  object?: number;
  volume?: number;
  range?: number;
  change?: SettingChange;
}
const rowOf = (c: SettingChange): StackRow => ({
  layer: c.owner.plate !== undefined ? 'plate' : c.scope, name: c.owner.name,
  value: c.now, object: c.owner.object, volume: c.owner.volume, range: c.owner.range, change: c,
});

export function valueStack(s: State, type: PresetType, key: string, slot = 0): StackRow[] {
  const rows: StackRow[] = [];
  const name = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const { base, user } = resolvePreset(s, type, name ?? '');
  const def = OPTIONS[key]?.default as ConfigValue | undefined;
  if (def !== undefined && base?.config[key] === undefined) rows.push({ layer: 'default', value: def });
  if (base?.config[key] !== undefined) rows.push({ layer: 'system', name: base.name, value: base.config[key] });
  if (user?.overrides[key] !== undefined && !same(key, user.overrides[key], rows.at(-1)?.value))
    rows.push({ layer: 'mine', name: user.name, value: user.overrides[key] });
  for (const c of selectChanges(s, type, slot)) if (c.key === key && c.scope === 'project') rows.push(rowOf(c));
  return rows;
}

export const overridesOf = (s: State, key: string): StackRow[] =>
  selectChanges(s, 'process').filter((c) => c.key === key && c.scope !== 'project').map(rowOf);

export const sourceOf = (s: State, type: PresetType, key: string, slot = 0) => valueStack(s, type, key, slot).at(-1);

export function revert(c: SettingChange) {
  const { object, volume, range } = c.owner;
  if (c.scope === 'object') return removeObjectOptions(object!, [c.key]);
  if (c.scope === 'part') return removeVolumeOptions(object!, volume!, [c.key]);
  if (c.scope === 'range') {
    if (c.key === 'layer_height') return editRange(object!, range!, { layer_height: Number(c.was ?? OPTIONS.layer_height?.default ?? 0.2) });
    return removeRangeOptions(object!, range!, [c.key]);
  }
  undoable(`Reset setting "${OPTIONS[c.key]?.label || c.key}"`, () => setState((s) => {
    const drop = (cfg: Config) => { const next = { ...cfg }; delete next[c.key]; return next; };
    const patch: Partial<State> = {};
    if (s.projectPresets) patch.projectPresets = c.type === 'filament'
      ? { ...s.projectPresets, filaments: s.projectPresets.filaments.map((f, i) => i === c.slot ? drop(f) : f) }
      : { ...s.projectPresets, [c.type]: drop(s.projectPresets[c.type]) };
    if (c.type === 'process') patch.projectPlates = s.projectPlates.map((p) => p.index === s.plateIndex ? { ...p, settings: drop(p.settings) } : p);
    const edits = drop(presetEdits(s, c.type, c.slot));
    if (c.type === 'filament') patch.filamentEdits = { ...s.filamentEdits, [s.filamentSlots[c.slot]]: edits };
    else patch.dirty = { ...s.dirty, [c.type]: edits };
    return patch;
  }));
}

export const changeCount = (s: State, type: PresetType, slot = 0) =>
  selectChanges(s, type, slot).filter((c) => c.scope === 'project' && c.owner.plate === undefined).length;

export function revertAll(type: PresetType, slot = 0) {
  const list = selectChanges(getState(), type, slot).filter((c) => c.scope === 'project' && c.owner.plate === undefined);
  if (list.length) undoable('Revert all', () => list.forEach(revert));
}

export function takeValue(type: PresetType, key: string, value: ConfigValue, slot = 0) {
  const s = getState();
  const change = selectChanges(s, type, slot).find((c) => c.scope === 'project' && c.key === key);
  if (change && same(key, value, presetValue(s, type, key, slot))) revert(change);
  else setOption(type, key, value, slot);
}
