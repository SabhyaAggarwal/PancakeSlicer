import type { Config, PresetType, SystemPreset, UserPreset } from '../api/types';
import { DEFAULT_PRINTER, getState, type State } from '../state/store';
import { baseVersion, catalogAnchor, changedKeys, profilesFor, resolveProfile } from './model';
import { selectCompatibility } from './compatibility';
import { defaultsInitialized, markDefaultsInitialized, rememberLocalProfile, importedBases } from '../persist/profiles';
import { defaultProcesses } from '../intent/default-process-model';
import { findProfile, profileKey } from './identity';
import { CURVED_DEFAULTS, surfaceValues } from '../intent/surface';

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

export function restoreCatalog(s: State): State {
  const bases = importedBases();
  if (!bases.length) return s;
  let result = s;
  for (const type of ['machine', 'process', 'filament'] as const) {
    const key = type === 'machine' ? 'machines' : type === 'process' ? 'processes' : 'filaments';
    const list = [...s[key]];
    let changed = false;
    for (const base of bases.filter((p) => p.type === type)) {
      const index = list.findIndex((p) => p.name === base.name);
      const current = list[index];
      if (current?.imported && baseVersion(current) === baseVersion(base)) continue;
      const shipped = String(current?.version ?? current?.config.version ?? '').split('.').map(Number);
      const installed = String(base.version ?? '').split('.').map(Number);
      const difference = shipped.map((n, i) => n - (installed[i] ?? 0)).find((n) => n !== 0);
      if (current && !current.imported && difference !== undefined && difference > 0) continue;
      if (index < 0) list.push(base); else list[index] = base;
      changed = true;
    }
    if (changed) result = { ...result, [key]: list };
  }
  return result;
}

export function ensureDefaultProfiles(s: State): State {
  s = restoreCatalog(s);
  if (!s.machines.length) return s;
  let next = { ...s, userPresets: [...s.userPresets] };
  const add = (base: SystemPreset, name = displayName(base), values: Config = {}) => {
    const profile = makeProfile(next, base, name, values);
    next.userPresets = [...next.userPresets,profile];
    rememberLocalProfile(profile, next.api?.mock);
    return profileKey(profile);
  };
  const own = (type: PresetType, name: string): string => {
    const profile = findProfile(next,type,name);
    if (profile) return profileKey(profile);
    const base = profilesFor(next, type).find((p) => p.name === name);
    if (!base) return name;
    const existing = next.userPresets.find((p) => p.type === type && p.base?.name === base.name && !Object.keys(p.overlay ?? p.overrides).length && !p.parent);
    return existing ? profileKey(existing) : add(base);
  };
  let preferred = '';
  try { preferred = localStorage.getItem('orca.printer') ?? ''; } catch {  }
  const known = (name: string) => next.machines.some((p) => p.name === name) || !!findProfile(next,'machine',name);
  next.printer = own('machine', next.printer || preferred || (known(DEFAULT_PRINTER) ? DEFAULT_PRINTER : next.machines[0].name));
  const printer = resolveProfile(next, 'machine', next.printer).base;
  if (!printer) return next;
  const processes = next.processes.filter((p) => selectCompatibility(next,'process',p.name).ok);
  let filaments = next.filaments.filter((p) => selectCompatibility({...next,process: next.process || processes[0]?.name || ''},'filament',p.name).ok);
  let firstFilament = filaments.find(p => first(p.config.filament_type) === 'PLA' && first(p.config.filament_vendor) === 'Generic') ?? filaments.find(p => first(p.config.filament_type) === 'PLA') ?? filaments[0];
  if (!defaultsInitialized() && processes.length && firstFilament) {
    const choices = defaultProcesses(next);
    const startingProcess = next.process;
    for (const choice of choices) {
      const base = next.processes.find((p) => p.name === choice.preset);
      if (!base) continue;
      const name = add(base, `My ${choice.label}`, { layer_height: String(choice.layer), ...surfaceValues(choice.surface),
        ...(choice.surface === 'smooth' ? CURVED_DEFAULTS : {}) });
      if (!next.process || choice.id === 'default:standard' && startingProcess === base.name) next.process = name;
    }
    markDefaultsInitialized();
  }
  if (!next.process && processes[0]) next.process = processes[0].name;
  next.process = own('process', next.process);
  filaments = next.filaments.filter(p => selectCompatibility(next,'filament',p.name).ok);
  firstFilament = filaments.find(p => first(p.config.filament_type) === 'PLA' && first(p.config.filament_vendor) === 'Generic') ?? filaments.find(p => first(p.config.filament_type) === 'PLA') ?? filaments[0];
  next.filamentSlots = (next.filamentSlots.length ? next.filamentSlots : firstFilament ? [firstFilament.name] : []).map((name) => own('filament', name || firstFilament?.name || ''));
  return next;
}
