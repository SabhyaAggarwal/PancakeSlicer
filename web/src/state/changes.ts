import type { ConfigValue, PresetType } from '../api/types';
import { sameValue } from './value-equality';
export { sameValue } from './value-equality';
import { isHidden, OPTIONS } from '../options';
import { effectiveConfig, PRESET_META, presetEdits, resolvePreset, type State } from './store';

export type ChangeScope = 'project' | 'object' | 'part' | 'range';
export interface ChangeTarget { object?: number; volume?: number; range?: number }
export interface ChangeOwner extends ChangeTarget { name: string; plate?: number }
export interface SettingChange {
  type: PresetType;
  slot: number;
  key: string;
  was: ConfigValue | undefined;
  now: ConfigValue;
  scope: ChangeScope;
  owner: ChangeOwner;
}


export function presetValue(s: State, type: PresetType, key: string, slot = 0): ConfigValue | undefined {
  const name = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  return resolvePreset(s, type, name ?? '').config[key] ?? OPTIONS[key]?.default as ConfigValue | undefined;
}

export function settingValue(s: State, type: PresetType, key: string, slot = 0, target: ChangeTarget = {}): ConfigValue | undefined {
  let value = effectiveConfig(s, type, slot)[key] ?? OPTIONS[key]?.default as ConfigValue | undefined;
  if (type !== 'process') return value;
  const object = s.objects.find((o) => o.id === target.object);
  if (!object) return value;
  value = object.settings[key] ?? value;
  const volume = object.volumes?.find((v) => v.id === target.volume);
  value = volume?.settings[key] ?? value;
  const range = object.ranges?.find((r) => r.id === target.range);
  return range ? key === 'layer_height' ? range.layer_height : range.settings[key] ?? value : value;
}

export function selectChanges(s: State, type: PresetType, slot = 0): SettingChange[] {
  const rows: SettingChange[] = [];
  const add = (key: string, was: ConfigValue | undefined, now: ConfigValue, scope: ChangeScope, owner: ChangeOwner) => {
    if (!isHidden(key) && !PRESET_META.test(key) && !sameValue(key, was, now)) rows.push({ type, slot, key, was, now, scope, owner });
  };
  const edits = presetEdits(s, type, slot);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const plate = type === 'process' ? s.projectPlates.find((p) => p.index === s.plateIndex) : undefined;
  const global = effectiveConfig(s, type, slot);
  const globalValue = (key: string) => global[key] ?? OPTIONS[key]?.default as ConfigValue | undefined;
  for (const key of new Set([...Object.keys(project ?? {}), ...Object.keys(plate?.settings ?? {}), ...Object.keys(edits)])) {
    const now = global[key];
    if (now === undefined) continue;
    const owner: ChangeOwner = edits[key] === undefined && plate?.settings[key] !== undefined
      ? { name: plate.name || `Plate ${plate.index}`, plate: plate.index } : { name: 'Project' };
    add(key, presetValue(s, type, key, slot), now, 'project', owner);
  }
  if (type !== 'process') return rows;
  for (const object of s.objects) {
    const objectValue = (key: string) => object.settings[key] ?? globalValue(key);
    for (const [key, now] of Object.entries(object.settings))
      add(key, globalValue(key), now, 'object', { name: object.name, object: object.id });
    for (const volume of object.volumes ?? []) for (const [key, now] of Object.entries(volume.settings))
      add(key, objectValue(key), now, 'part', { name: `${object.name} › ${volume.name}`, object: object.id, volume: volume.id });
    for (const range of object.ranges ?? []) {
      const owner = { name: `${object.name} › Range ${range.z[0].toFixed(2)}-${range.z[1].toFixed(2)} mm`, object: object.id, range: range.id };
      add('layer_height', objectValue('layer_height'), range.layer_height, 'range', owner);
      const parts = object.volumes?.filter((v) => v.type === 'part') ?? [];
      for (const [key, now] of Object.entries(range.settings)) {
        if (key === 'layer_height') continue;
        if (!parts.length) add(key, objectValue(key), now, 'range', owner);
        else for (const part of parts) add(key, part.settings[key] ?? objectValue(key), now, 'range', {
          ...owner, name: `${owner.name} › ${part.name}`, volume: part.id,
        });
      }
    }
  }
  return rows;
}
