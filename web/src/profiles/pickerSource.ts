import type { Config, PresetType } from '../api/types';
import { getState, resolvePreset, toast } from '../state/store';
import { profileKey } from './identity';
import { selectCompatibility } from './compatibility';
import * as catalog from './catalog';
import { requestPreset } from '../components/PresetCombo';

export interface PickerEntry {
  key: string;
  name: string;
  config: Config;
  vendor: string;
  mine: boolean;
  reason?: string;
}

const inUse = (type: PresetType) => { const s = getState(); return type === 'machine' ? [s.printer] : type === 'process' ? [s.process] : s.filamentSlots; };

export function listProfiles(type: PresetType): PickerEntry[] {
  const s = getState();
  return catalog.listProfiles(s,type).map(row => {
    const r = resolvePreset(s,type,row.key);
    return {key:row.key,name:row.name,config:r.config,vendor:r.base?.vendor ?? '',mine:true};
  });
}

export function listCatalog(type: PresetType): PickerEntry[] {
  const s = getState();
  return catalog.listCatalog(s,type).filter(row => row.compatible.ok).map(row => ({key:row.name,name:row.name,
    config:resolvePreset(s,type,row.name).config,vendor:row.vendor,mine:false}));
}

export async function createProfileFromBase(type: PresetType, base: string, slot = 0, select: (name: string) => void = (name) => requestPreset(type, slot, name)) {
  const s = getState();
  const owned = s.userPresets.filter(p => p.type === type && !p.archived && p.kind !== 'master' && (p.base?.name ?? p.inherits) === base && selectCompatibility(s,type,profileKey(p)).ok);
  try {
  const name = (owned.find(p => inUse(type).includes(profileKey(p)) || inUse(type).includes(p.name))?.id ?? owned[0]?.id) ?? await catalog.createProfileFromBase(type, base);
  select(name);
  } catch(error){toast(`Add profile failed: ${error instanceof Error ? error.message : String(error)}`);}
}
