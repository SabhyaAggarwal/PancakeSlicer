import type { Config, PresetType } from '../api/types';
import { getState, isCompatible, resolvePreset } from '../state/store';
import * as catalog from './catalog';
import { requestPreset } from '../components/PresetCombo';

export interface PickerEntry {
  name: string;
  config: Config;
  vendor: string;
  mine: boolean;
  reason?: string;
}

const inUse = (type: PresetType) => { const s = getState(); return type === 'machine' ? [s.printer] : type === 'process' ? [s.process] : s.filamentSlots; };

export function listProfiles(type: PresetType): PickerEntry[] {
  const s = getState();
  const names = [...new Set([...catalog.listProfiles(s, type).map((p) => p.name), ...inUse(type)])].filter(Boolean);
  return names.map((name) => {
    const r = resolvePreset(s, type, name);
    return { name, config: r.config, vendor: r.base?.vendor ?? '', mine: !!r.user };
  });
}

export function listCatalog(type: PresetType): PickerEntry[] {
  const s = getState();
  const rows = catalog.listCatalog(s, type);
  if (rows.length) return rows.map((r) => ({ name: r.name, config: resolvePreset(s, type, r.name).config, vendor: r.vendor, mine: false, reason: r.compatible.ok ? undefined : r.compatible.reason ?? 'Not for this printer' }));
  const printerBase = resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer;
  const bases = type === 'machine' ? s.machines : type === 'process' ? s.processes : s.filaments;
  return bases.map((p) => ({ name: p.name, config: p.config, vendor: p.vendor, mine: false,
    reason: type === 'machine' || isCompatible(p, printerBase, s.process) ? undefined : 'Not for this printer' }));
}

export function createProfileFromBase(type: PresetType, base: string, slot = 0, select: (name: string) => void = (name) => requestPreset(type, slot, name)) {
  const s = getState();
  const owned = s.userPresets.filter(p => p.type === type && (p.base?.name ?? p.inherits) === base);
  const name = owned.find(p => inUse(type).includes(p.name))?.name ?? owned[0]?.name ?? catalog.createProfileFromBase(type, base);
  select(name);
}
