import type { ProjectProfileReferences } from './ownership';
import type { Config, PresetType } from '../api/types';
import { resolveProfile, type ProfileCatalog, type ResolvedProfile } from './model';
export interface ProfileContextState extends ProfileCatalog {
  profileSnapshots?:ProjectProfileReferences|null;
  printer: string; process: string; filamentSlots?: string[];
  dirty?: { machine: Config; process: Config }; filamentEdits?: Record<string, Config>;
  projectPresets?: { machine: Config; process: Config; filaments: Config[] } | null;
  projectPlates?: { index: number; settings?: Config }[]; plateIndex?: number;
}
const empty: Config = {};
type EditCache = WeakMap<Config, Config>;
type PlateCache = WeakMap<Config, EditCache>;
const effective = new WeakMap<Config, WeakMap<Config, PlateCache>>();
const snapshots = new WeakMap<ProfileContextState, Map<string, Config>>();
export function effectiveProfileValues(s: ProfileContextState, type: PresetType, key: string, slot = 0): Config {
  const selection = `${type}:${slot}:${key}`;
  let snapshot = snapshots.get(s);
  if (!snapshot) snapshots.set(s, (snapshot = new Map()));
  const previous = snapshot.get(selection);
  if (previous) return previous;
  const resolved=resolveProfile(s,type,key), captured=type==='machine'?s.profileSnapshots?.printer:type==='process'?s.profileSnapshots?.process:s.profileSnapshots?.filaments[slot];
  const config=captured && (captured.id===key || captured.id===resolved.key) ? captured.config : resolved.config, project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const plate = type === 'process' ? s.projectPlates?.find(p => p.index === s.plateIndex)?.settings : undefined;
  const edits = (type === 'filament' ? s.filamentEdits?.[key] : s.dirty?.[type]) ?? empty;
  let projects = effective.get(config); if (!projects) effective.set(config, (projects = new WeakMap()));
  let plates = projects.get(project ?? empty); if (!plates) projects.set(project ?? empty, (plates = new WeakMap()));
  let editsCache = plates.get(plate ?? empty); if (!editsCache) plates.set(plate ?? empty, (editsCache = new WeakMap()));
  const prior = editsCache.get(edits);
  if (prior) {snapshot.set(selection, prior); return prior;}
  const value = {...config, ...project, ...plate, ...edits}; editsCache.set(edits,value); snapshot.set(selection,value); return value;
}
export interface ContextProfile extends ResolvedProfile { id?: string; revision: number; baseName?: string; baseVendor?: string; parentName?: string }
const first = (value: Config[string] | undefined) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '');
export function profileContext(s: ProfileContextState, overrides: {printer?: string; process?: string; nozzle?: string | number} = {}) {
  const printerKey = overrides.printer ?? s.printer, processKey = overrides.process ?? s.process;
  const entry = (type: PresetType, key: string, slot = 0): ContextProfile => {
    const resolved = resolveProfile(s, type, key), raw = resolved.source;
    const selected = type === 'machine' ? key === s.printer : type === 'process' ? key === s.process : key === s.filamentSlots?.[slot];
    return {...resolved, id: resolved.key, revision: raw?.revision ?? 0, baseName: resolved.base?.name, baseVendor: resolved.base?.vendor,
      parentName: raw?.parentId ? resolveProfile(s,type,raw.parentId).source?.name : raw?.parent ?? raw?.inherits,
      config: selected ? effectiveProfileValues(s,type,key,slot) : resolved.config};
  };
  const printer = entry('machine', printerKey), process = entry('process', processKey);
  if (overrides.nozzle !== undefined) printer.config = {...printer.config, nozzle_diameter:[String(overrides.nozzle)]};
  const nozzleDiameters = [printer.config.nozzle_diameter ?? []].flat().map(Number).filter(n => Number.isFinite(n) && n > 0);
  return {ownerPrinterId: printer.id, printer, process, filaments:(s.filamentSlots ?? []).map((key,slot) => entry('filament',key,slot)),
    nozzleDiameters, printerModel:first(printer.config.printer_model), printerVariant:first(printer.config.printer_variant),
    kinematics:first(printer.config.nonplanar_kinematics ?? printer.config.printer_kinematics ?? printer.config.printer_structure)};
}
