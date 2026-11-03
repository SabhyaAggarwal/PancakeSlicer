import type { Config, PresetType, SystemPreset, UserPreset } from '../api/types';
import { getState, type State } from '../state/store';
import { catalogAnchor, changedKeys, profilesFor, resolveProfile } from './model';
import { selectCompatibility } from './compatibility';
import { profileKey } from './identity';

export interface ProfileRow {
  key: string;
  name: string;
  base: string;
  layerHeight?: number;
  nozzle?: number;
  material?: string;
  vendor?: string;
  mine: true;
  changed: boolean;
}
export interface CatalogRow {
  name: string;
  vendor: string;
  model?: string;
  material?: string;
  layerHeight?: number;
  compatible: { ok: boolean; reason?: string };
}
export interface CatalogFilters {
  vendor?: string;
  model?: string;
  material?: string;
  printer?: string;
  nozzle?: string | number;
  search?: string;
  browseAll?: boolean;
}

const first = (value: Config[string] | undefined) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '');
const number = (value: Config[string] | undefined) => { const n = Number(first(value)); return first(value) !== '' && Number.isFinite(n) ? n : undefined; };
const metadata = (config: Config) => ({ layerHeight: number(config.layer_height), nozzle: number(config.nozzle_diameter), material: first(config.filament_type) || undefined });

const profileRows = new WeakMap<State, Map<PresetType, ProfileRow[]>>();
const catalogRows = new WeakMap<State, Map<string, CatalogRow[]>>();

export function listProfiles(s: State, type: PresetType, filters: {browseAll?: boolean} = {}): ProfileRow[] {
  let cache = profileRows.get(s);
  if (!cache) profileRows.set(s, (cache = new Map()));
  if (!filters.browseAll && cache.has(type)) return cache.get(type)!;
  const rows: ProfileRow[] = s.userPresets.filter((p) => p.type === type && !p.archived && (p.kind ?? 'profile') === 'profile' && (filters.browseAll || selectCompatibility(s,type,profileKey(p)).ok)).map((p) => {
    const { config, base } = resolveProfile(s, type, profileKey(p));
    return { key: profileKey(p), name: p.name, base: p.base?.name ?? p.inherits, ...metadata(config), vendor: base?.vendor,
      mine: true, changed: Object.keys(p.overlay ?? p.overrides).length > 0 };
  });
  if (!filters.browseAll) cache.set(type, rows);
  return rows;
}
export function listCatalog(s: State, type: PresetType, filters: CatalogFilters = {}): CatalogRow[] {
  let cache = catalogRows.get(s);
  if (!cache) catalogRows.set(s, (cache = new Map()));
  const key = JSON.stringify([type, filters.vendor, filters.model, filters.material, filters.printer, filters.nozzle, filters.search, filters.browseAll]);
  if (cache.has(key)) return cache.get(key)!;
  const rows = profilesFor(s, type).filter((p) => (!filters.vendor || p.vendor === filters.vendor)
    && (!filters.model || (p.model ?? first(p.config.printer_model)) === filters.model)
    && (!filters.material || first(p.config.filament_type) === filters.material)
    && (!filters.search || `${p.name} ${p.vendor} ${p.model ?? ''}`.toLowerCase().includes(filters.search.toLowerCase())))
    .map((p) => ({ name: p.name, vendor: p.vendor, model: (p.model ?? first(p.config.printer_model)) || undefined,
      ...metadata(p.config), compatible: selectCompatibility(s, type, p.name, filters.printer ?? s.printer, filters.nozzle) })).filter(row => filters.browseAll || row.compatible.ok);
  cache.set(key, rows);
  return rows;
}

const displayName = (base: SystemPreset) => `My ${base.name.replace(/\s*@.*$/, '').replace(/\s+nozzle\b/i, '').trim()}`;
function uniqueName(s: State, type: PresetType, desired: string): string {
  let name = desired, n = 2;
  while (s.userPresets.some((p) => p.type === type && p.name === name) || profilesFor(s, type).some((p) => p.name === name)) name = `${desired} (${n++})`;
  return name;
}
function makeProfile(s: State, base: SystemPreset, name: string, values: Config = {}): UserPreset {
  const overlay = changedKeys(values, base.config);
  return { id: `profile:${crypto.randomUUID()}`, revision: 0, kind: 'profile', name: uniqueName(s, base.type, name), type: base.type, inherits: base.name, base: catalogAnchor(base), snapshot: {base: catalogAnchor(base),config: {...base.config,...overlay}},
    overlay, overrides: overlay, baseline: Object.fromEntries(Object.keys(overlay).map((key) => [key, base.config[key] ?? null])) };
}

export async function createProfileFromBase(type:PresetType,baseName:string,name?:string):Promise<string> {
  const s=getState(),base=profilesFor(s,type).find(p=>p.name === baseName);
  if(!base)throw new Error('Base unavailable');
  const profile=makeProfile(s,base,name?.trim() || displayName(base));
  const {commitProfileRecords}=await import('./transactions');
  await commitProfileRecords([profile]);return profileKey(profile);
}
