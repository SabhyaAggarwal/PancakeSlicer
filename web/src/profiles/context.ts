import type { ProjectProfileReferences } from './ownership';
import type { Config, PresetType } from '../api/types';
import { resolveProfile, type ProfileCatalog, type ResolvedProfile } from './model';
import { availableHeights, nearestHeight, resolveDynamic, type DynamicContext, type DynamicRecord, type PrintContext } from './dynamic';
export interface ProfileContextState extends ProfileCatalog {
  profileSnapshots?:ProjectProfileReferences|null;
  printer: string; process: string; filamentSlots?: string[];
  dirty?: { machine: Config; process: Config }; filamentEdits?: Record<string, Config>;
  projectPresets?: { machine: Config; process: Config; filaments: Config[] } | null;
  projectPlates?: { index: number; settings?: Config }[]; plateIndex?: number;
  processVariant?: { layerHeight: number } | null;
}
const empty: Config = {};
type EditCache = WeakMap<Config, Config>;
type PlateCache = WeakMap<Config, EditCache>;
const effective = new WeakMap<Config, WeakMap<Config, PlateCache>>();
const snapshots = new WeakMap<ProfileContextState, Map<string, Config>>();
const scalar = (value: Config[string] | undefined) => Array.isArray(value) ? value[0] : typeof value === 'string' ? value.split(/[;,]/)[0] : value;
const dynamicOf = (resolved: ResolvedProfile) => (resolved.user as DynamicRecord | undefined)?.dynamic ? resolved.user as DynamicRecord : undefined;
export function printContext(s: ProfileContextState): PrintContext {
  const machine = effectiveProfileValues(s, 'machine', s.printer);
  return { printerModel: String(scalar(machine.printer_model) ?? ''), nozzle: Number(scalar(machine.nozzle_diameter)), printerId: resolveProfile(s, 'machine', s.printer).key };
}
export function processHeights(s: ProfileContextState): number[] {
  const p = dynamicOf(resolveProfile(s, 'process', s.process));
  return p ? availableHeights(p, printContext(s)) : [];
}
export function effectiveLayerHeight(s: ProfileContextState): number | null {
  const p = dynamicOf(resolveProfile(s, 'process', s.process));
  if (!p) return null;
  const heights = availableHeights(p, printContext(s));
  return heights.length ? nearestHeight(heights, s.processVariant?.layerHeight ?? p.dynamic!.reference.layerHeight) : null;
}
const dynamicValues = new WeakMap<Config, Map<string, Config>>();
function withDynamic(s: ProfileContextState, type: PresetType, resolved: ResolvedProfile, config: Config): Config {
  const p = dynamicOf(resolved);
  if (!p) return config;
  const own = type === 'machine' ? { printerModel: String(scalar(config.printer_model) ?? ''), nozzle: Number(scalar(config.nozzle_diameter)) } : printContext(s);
  const layerHeight = type === 'process' ? effectiveLayerHeight(s) : p.dynamic!.reference.layerHeight;
  if (!(own.nozzle > 0) || layerHeight === null) return config;
  const ctx: DynamicContext = { printerModel: own.printerModel, nozzle: own.nozzle, layerHeight }, id = `${ctx.printerModel}|${ctx.nozzle}|${layerHeight}`;
  let byContext = dynamicValues.get(config);
  if (!byContext) dynamicValues.set(config, (byContext = new Map()));
  let value = byContext.get(id);
  if (!value) byContext.set(id, (value = { ...config, ...resolveDynamic(p, ctx, config) }));
  return value;
}
const resolvedDynamic = new WeakMap<ResolvedProfile, Map<string, ResolvedProfile>>();
export function resolvedWithDynamic(s: ProfileContextState, type: PresetType, resolved: ResolvedProfile): ResolvedProfile {
  const config = withDynamic(s, type, resolved, resolved.config);
  if (config === resolved.config) return resolved;
  let byConfig = resolvedDynamic.get(resolved);
  if (!byConfig) resolvedDynamic.set(resolved, (byConfig = new Map()));
  const id = JSON.stringify([config.layer_height, config.nozzle_diameter, config.printer_model]);
  let value = byConfig.get(id);
  if (!value || value.config !== config) byConfig.set(id, (value = { ...resolved, config }));
  return value;
}

export function effectiveProfileValues(s: ProfileContextState, type: PresetType, key: string, slot = 0): Config {
  const selection = `${type}:${slot}:${key}`;
  let snapshot = snapshots.get(s);
  if (!snapshot) snapshots.set(s, (snapshot = new Map()));
  const previous = snapshot.get(selection);
  if (previous) return previous;
  const resolved=resolveProfile(s,type,key), captured=type==='machine'?s.profileSnapshots?.printer:type==='process'?s.profileSnapshots?.process:s.profileSnapshots?.filaments[slot];
  const pinned=!!captured && (captured.id===key || captured.id===resolved.key) && !dynamicOf(resolved);
  const config=pinned ? captured!.config : withDynamic(s, type, resolved, resolved.config), project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
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
