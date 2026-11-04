import type { ProjectProfileReferences } from './ownership';
import type { Config, PresetType } from '../api/types';
import { resolveProfile, type ProfileCatalog, type ResolvedProfile } from './model';
import { autoValues, AUTO_KEYS, heightsFor, type Speed } from './auto-rules';
import { chainApplicability, type AutoRecord, type ChainLink } from './chain';
import { findProfile } from './identity';
export interface ProfileContextState extends ProfileCatalog {
  profileSnapshots?:ProjectProfileReferences|null;
  printer: string; process: string; filamentSlots?: string[];
  dirty?: { machine: Config; process: Config }; filamentEdits?: Record<string, Config>;
  projectPresets?: { machine: Config; process: Config; filaments: Config[] } | null;
  projectPlates?: { index: number; settings?: Config }[]; plateIndex?: number;
  processVariant?: { layerHeight: number } | null;
  speed?: Speed;
}
const empty: Config = {};
type EditCache = WeakMap<Config, Config>;
type PlateCache = WeakMap<Config, EditCache>;
const effective = new WeakMap<Config, WeakMap<Config, PlateCache>>();
const snapshots = new WeakMap<ProfileContextState, Map<string, Config>>();
const scalar = (value: Config[string] | undefined) => Array.isArray(value) ? value[0] : typeof value === 'string' ? value.split(/[;,]/)[0] : value;
const number = (value: Config[string] | undefined) => { const x = Number(scalar(value)); return Number.isFinite(x) ? x : 0; };
const chains = new WeakMap<ResolvedProfile, ChainLink[]>();
export function profileChain(s: ProfileContextState, type: PresetType, resolved: ResolvedProfile): ChainLink[] {
  let links = chains.get(resolved);
  if (links) return links;
  links = [];
  for (let p = resolved.user, seen = new Set<string>(); p && links.length < 4 && !seen.has(String(p.id)); p = p.parentId ? findProfile(s, type, p.parentId) : undefined) {
    seen.add(String(p.id));
    links.unshift({ ...(p as unknown as ChainLink), type, id: p.id, name: p.name, overlay: p.overlay ?? p.overrides ?? {} });
  }
  chains.set(resolved, links);
  return links;
}
export const worksWithRuleOf = chainApplicability;
export const chainAuto = (links: ChainLink[]): AutoRecord => ({
  preferredHeight: [...links].reverse().find((l) => l.auto?.preferredHeight)?.auto?.preferredHeight,
  preferredSpeed: [...links].reverse().find((l) => l.auto?.preferredSpeed)?.auto?.preferredSpeed,
});
export function effectiveLayerHeight(s: ProfileContextState): number {
  return s.processVariant?.layerHeight ?? chainAuto(profileChain(s, 'process', resolveProfile(s, 'process', s.process))).preferredHeight ?? 0.2;
}
export function effectiveSpeed(s: ProfileContextState): Speed {
  return s.speed ?? chainAuto(profileChain(s, 'process', resolveProfile(s, 'process', s.process))).preferredSpeed ?? 'balanced';
}
export function availableHeights(s: ProfileContextState): number[] {
  const machine = effectiveProfileValues(s, 'machine', s.printer);
  return heightsFor(number(machine.nozzle_diameter), number(machine.min_layer_height), number(machine.max_layer_height));
}
export type SettingOrigin = 'set' | 'inherited' | 'auto';
export function originsOf(links: ChainLink[]): Record<string, SettingOrigin> {
  const out: Record<string, SettingOrigin> = {};
  for (const k of AUTO_KEYS) out[k] = 'auto';
  links.forEach((l, i) => { for (const k of Object.keys(l.overlay)) if (k !== 'layer_height') out[k] = i === links.length - 1 ? 'set' : 'inherited'; });
  return out;
}
const autoCache = new WeakMap<Config, Map<string, Config>>();
function withAuto(s: ProfileContextState, type: PresetType, resolved: ResolvedProfile, config: Config): Config {
  if (type !== 'process') return config;
  const machine = effectiveProfileValues(s, 'machine', s.printer), links = profileChain(s, type, resolved);
  const nozzle = number(machine.nozzle_diameter);
  if (!(nozzle > 0)) return config;
  const volumetric = (s.filamentSlots ?? []).map((k, i) => number(effectiveProfileValues(s, 'filament', k, i).filament_max_volumetric_speed)).filter((v) => v > 0);
  const preferred = chainAuto(links), layerHeight = s.processVariant?.layerHeight ?? preferred.preferredHeight ?? 0.2, speed = s.speed ?? preferred.preferredSpeed ?? 'balanced';
  const inputs = { nozzle, layerHeight, speed, maxSpeed: number(machine.machine_max_speed_x), maxAccel: number(machine.machine_max_acceleration_extruding), maxVolumetric: volumetric.length ? Math.min(...volumetric) : undefined };
  const id = JSON.stringify(inputs);
  let byInputs = autoCache.get(config);
  if (!byInputs) autoCache.set(config, (byInputs = new Map()));
  let value = byInputs.get(id);
  if (!value) {
    const computed = autoValues({ ...inputs, intent: config }), pinned = new Set(links.flatMap((l) => Object.keys(l.overlay)));
    for (const k of pinned) if (k !== 'layer_height') delete computed[k];
    byInputs.set(id, (value = { ...config, ...computed }));
  }
  return value;
}
const resolvedAuto = new WeakMap<ResolvedProfile, Map<Config, ResolvedProfile>>();
export function resolvedWithAuto(s: ProfileContextState, type: PresetType, resolved: ResolvedProfile): ResolvedProfile {
  const config = withAuto(s, type, resolved, resolved.config);
  if (config === resolved.config) return resolved;
  let byConfig = resolvedAuto.get(resolved);
  if (!byConfig) resolvedAuto.set(resolved, (byConfig = new Map()));
  let value = byConfig.get(config);
  if (!value) byConfig.set(config, (value = { ...resolved, config }));
  return value;
}
export function resolveAuto(s: ProfileContextState, type: PresetType = 'process'): { config: Config; sources: Record<string, SettingOrigin> } {
  const key = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots?.[0] ?? '';
  return { config: effectiveProfileValues(s, type, key), sources: originsOf(profileChain(s, type, resolveProfile(s, type, key))) };
}

export function effectiveProfileValues(s: ProfileContextState, type: PresetType, key: string, slot = 0): Config {
  const selection = `${type}:${slot}:${key}`;
  let snapshot = snapshots.get(s);
  if (!snapshot) snapshots.set(s, (snapshot = new Map()));
  const previous = snapshot.get(selection);
  if (previous) return previous;
  const resolved=resolveProfile(s,type,key), captured=type==='machine'?s.profileSnapshots?.printer:type==='process'?s.profileSnapshots?.process:s.profileSnapshots?.filaments[slot];
  const pinned=!!captured && (captured.id===key || captured.id===resolved.key) && type !== 'process';
  const config=pinned ? captured!.config : withAuto(s, type, resolved, resolved.config), project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
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
