import type { Config, PresetType, SystemPreset, UserPreset } from '../api/types';
import { getState, type State } from '../state/store';
import { catalogAnchor, catalogKey, changedKeys, findCatalogBase, profilesFor, resolveProfile } from './model';
import { selectCompatibility } from './compatibility';
import { findProfile, profileKey } from './identity';
import type { ProfileCreation } from './transactions';
export { catalogKey } from './model';

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
  key: string;
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
    .map((p) => ({ key: catalogKey(p), name: p.name, vendor: p.vendor, model: (p.model ?? first(p.config.printer_model)) || undefined,
      ...metadata(p.config), compatible: selectCompatibility(s, type, catalogKey(p), filters.printer ?? s.printer, filters.nozzle) })).filter(row => filters.browseAll || row.compatible.ok);
  cache.set(key, rows);
  return rows;
}

export function resolveCatalogEntry(s: State, type: PresetType, key: string) {
  const found = findCatalogBase(s,type,key);
  if (!found.base) throw new Error(found.ambiguous ? 'This catalog name is ambiguous. Choose its exact source again.' : 'This catalog source is unavailable. Reopen Add and choose it again.');
  return {key: catalogKey(found.base), base: found.base, anchor: catalogAnchor(found.base)};
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

const pendingCreations=new Map<string,{creation:ProfileCreation;flight?:Promise<string>}>();
const baseIdentity=catalogKey;
export async function createProfileFromBase(type:PresetType,baseName:string,name?:string):Promise<string> {
  const {key:identity}=resolveCatalogEntry(getState(),type,baseName),explicit=name?.trim();
  const {profileCreation}=await import('./transactions');
  let entry=explicit?undefined:pendingCreations.get(identity);
  if(!entry){
    entry={creation:profileCreation(s=>{
      const current=findCatalogBase(s,type,identity).base;
      if(!current || baseIdentity(current)!==identity)throw new Error('The catalog source changed. Reopen Add and choose it again.');
      if(!explicit){
        const selected=type==='machine'?[s.printer]:type==='process'?[s.process]:s.filamentSlots;
        const active=new Set(selected.flatMap(key=>{const p=findProfile(s,type,key);return p?[profileKey(p)]:[];}));
        const owned=s.userPresets.filter(p=>{
          if(p.type!==type || p.archived || p.kind==='master')return false;
          const bound=findCatalogBase(s,type,p.base ?? p.inherits).base;
          return !!bound && baseIdentity(bound)===identity && selectCompatibility(s,type,profileKey(p)).ok;
        });
        const reuse=owned.find(p=>active.has(profileKey(p))) ?? owned.sort((a,b)=>profileKey(a).localeCompare(profileKey(b)))[0];
        if(reuse)return profileKey(reuse);
      }
      return makeProfile(s,current,explicit || displayName(current));
    })};
    if(!explicit)pendingCreations.set(identity,entry);
  }
  if(!entry.flight){
    const own=entry;
    own.flight=own.creation.commit().then(key=>{if(pendingCreations.get(identity)===own)pendingCreations.delete(identity);return key;},error=>{
      own.flight=undefined;if(!own.creation.retryable && pendingCreations.get(identity)===own)pendingCreations.delete(identity);throw error;
    });
  }
  return entry.flight!;
}
