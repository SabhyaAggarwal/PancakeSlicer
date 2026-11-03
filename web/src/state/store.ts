import { timeInteraction } from '../interaction-metrics';
import { app } from '../flow/app';
import { tools } from '../flow/tools';
import { useSyncExternalStore } from 'react';
import { plateSignature } from '../slice-input';
import * as THREE from 'three';
import type { Api } from '../api/client';
import type { Config, ConfigValue, JobStatus, PresetType, SystemPreset, UserPreset } from '../api/types';
import type { ProjectPlate } from '../plater/threemf';
import { OPTIONS, type OptionMode } from '../options';
import { undo, undoable } from './history';
import { selectChanges } from './changes';
import { canEditProject } from '../api/client';
import type { PaintKind, Painting } from '../plater/paint';
import { decideConflict, prepareProfile, preserveProfileParents, profileConflicts, resolveProfile, saveProfile } from '../profiles/model';
import { rememberProfile, rememberProfileUpdate } from '../persist/profiles';
import { noticeKind, shouldToast, type NoticeEntry, type NoticeKind } from '../notice-text';
import { ensureDefaultProfiles } from '../profiles/catalog';
export { PRESET_META } from '../profiles/model';
import { isCompatible } from '../profiles/compatibility';
export { isCompatible, selectCompatibility } from '../profiles/compatibility';


export interface Instance {
  id: number;
  x: number;
  y: number;
  rotZ: number;
  native?: { json: Record<string, unknown>; x: number; y: number; z: number; rotZ: number };
  z?: number;
  printable?: boolean;
}

export type SendPrinter = { name: string; kind: string; editable?: boolean; host?: string; serial?: string; model?: string; url?: string; auth?: string; username?: string };
export type VolumeType = 'part' | 'negative' | 'modifier' | 'support_blocker' | 'support_enforcer' | 'seam';
export type SeamMode = 'center' | 'left' | 'right' | 'enforced' | 'blocked' | 'neutral';

export interface Volume {
  seamMode?: SeamMode;
  id: number;
  name: string;
  type: VolumeType;
  geometry: THREE.BufferGeometry;
  matrix: number[];
  extruder: number;
  native?: number;
  settings: Config;
  paint?: Partial<Record<PaintKind, Painting>>;
  text?: TextSpec;
  svg?: SvgSpec;
}

export interface SvgSpec {
  svg: string;
  name: string;
  depth: number;
  size?: number;
  surface?: TextSpec['surface'];
}

export interface TextSpec {
  text: string;
  font: string;
  size: number;
  depth: number;
  bold: boolean;
  italic: boolean;
  lineSpacing: number;
  charGap: number;
  align: 'left' | 'center' | 'right';
  surface?: { at: [number, number, number]; normal: [number, number, number] };
}

export interface NativeObject {
  entry: Record<string, unknown>;
  frame: number[];
  base: { rotX: number; rotY: number; scale: [number, number, number] };
  main: number;
  files: Record<string, Blob>;
  layerProfile?: number[];
}

export interface LayerRange { id: number; z: [number, number]; layer_height: number; settings: Config }

export interface Mix { components: number[]; ratios: number[]; gradient: 0 | 1 | null; perPart: boolean }

export interface PlateObject {
  id: number;
  name: string;
  geometry: THREE.BufferGeometry;
  rotX: number;
  rotY: number;
  scale: [number, number, number];
  extruder: number;
  instances: Instance[];
  source: File;
  simplification?: { trianglesBefore: number; trianglesAfter: number; maxErrorMm: number; kept: 'simplified' | 'original' };
  settings: Config;
  volumes?: Volume[];
  native?: NativeObject;
  ranges?: LayerRange[];
  paint?: Partial<Record<PaintKind, Painting>>;
  brimEars?: { p: [number, number, number]; r: number }[];
  text?: TextSpec;
  svg?: SvgSpec;
  units?: 'inch' | 'meter';
  layerProfile?: number[];
  project?: { buildIndex?: number; plateIndex?: number; plateOrigin?: [number, number]; file: string; placeholder: boolean; missing?: boolean; origin?: [number, number, number];
    saved?: { object: number; instance: number; instanceId: number; extruder: number; rotZ: number; item?: number[] };
    paintable?: boolean; paint?: Partial<Record<'support' | 'seam' | 'fuzzy' | 'color', Record<string, string>>>; partTris?: number[] };
}

export interface LayerEvent {
  z: number;
  type: 'pause' | 'custom' | 'template' | 'colour' | 'change';
  text?: string;
}

export interface HeadInfo {
  id: string;
  name: string;
  calibrated: boolean;
  user?: boolean;
}

export type PresetUx =
  | { kind: 'save'; type: PresetType; slot: number }
  | { kind: 'compare'; type: PresetType; left?: string; right?: string }
  | { kind: 'presets'; type: PresetType; name?: string; edit?: boolean }
  | { kind: 'export' }
  | { kind: 'import'; back?: PresetUx };

export type MainTab = 'projects' | 'prepare' | 'preview';
export type CalibMenu = 'temperature' | 'max-flowrate' | 'pressure-advance' | 'flow-ratio' | 'retraction' | 'cornering' | 'input-shaping-freq' | 'input-shaping-damp' | 'vfa' | 'nonplanar';
export type Gizmo = 'none' | 'move' | 'rotate' | 'scale' | 'flatten' | 'cut' | 'measure' | 'layers' | 'brimears' | 'boolean' | 'text' | 'svg' | PaintKind;

export interface JobView {
  id?: string;
  state: 'idle' | 'submitting' | 'running' | 'done' | 'failed' | 'canceled';
  pct: number;
  stage: string;
  error?: string;
  status?: JobStatus;
  stale?: boolean;
  plateSignature?: string;
  warnings?: string[];
  calib?: string;
  calibLog?: string[];
  exported?: string;
}

export interface CutPlane {
  z: number;
  rotX: number;
  rotY: number;
  keepUpper: boolean;
  keepLower: boolean;
  placeLower: boolean;
  flipUpper?: boolean;
  toParts?: boolean;
  mode?: 'planar' | 'dovetail';
  connectors?: CutConnector[];
  connector?: CutConnectorAttribs;
  selectedConnectors?: number[];
  editing?: boolean;
  groove?: CutGroove;
}

export interface CutConnector { pos: [number, number]; a: CutConnectorAttribs }

export interface CutConnectorAttribs {
  type: 'plug' | 'dowel' | 'snap';
  style: 'prism' | 'frustum';
  shape: 'triangle' | 'square' | 'hexagon' | 'circle';
  depth: number; depthTolerance: number; size: number; sizeTolerance: number; angle: number;
  bulge: number; space: number;
}

export interface CutGroove { depth: number; depthTolerance: number; width: number; widthTolerance: number; flapsAngle: number; angle: number; count: number; gap: number }

export interface State {
  api: Api | null;
  features: { native_support?: boolean; emboss_svg?: boolean; emboss_svg_surface?: boolean; plate_locks?: boolean; mesh_simplify_error?: boolean; export_drc?: boolean; export_generic_3mf?: boolean; mesh_boolean?: boolean; emboss_text?: boolean; emboss_surface?: boolean; emboss_surface_outside?: boolean; cut?: boolean; volumes?: boolean; edit?: boolean; mesh_plates?: boolean; paint?: boolean; layer_profile?: boolean; arrange?: boolean; project_volumes?: boolean; project_paint?: boolean; orient_scores?: boolean; floating?: boolean;
    ranges?: boolean; moves_extended?: boolean; mixed_temperatures?: boolean; arrange_materials?: boolean; wipe_tower?: boolean; explode?: boolean; cut_connectors?: boolean; mesh_repair?: boolean; mesh_simplify?: boolean; brim_ears?: boolean; seam_parts?: boolean };
  cut: CutPlane | null;
  loading: string | null;
  machines: SystemPreset[];
  processes: SystemPreset[];
  filaments: SystemPreset[];
  userPresets: UserPreset[];
  printer: string;
  process: string;
  filamentSlots: string[];
  dirty: Record<'machine' | 'process', Config>;
  filamentEdits: Record<string, Config>;
  projectPresets: { machine: Config; process: Config; filaments: Config[] } | null;
  projectNotice: string | null;
  bedType: string;
  uiMode: OptionMode;
  developerMode: boolean;
  mixedTemperatures: boolean;
  tab: MainTab;
  dialog: { type: 'machine' | 'filament'; slot: number } | null;
  objects: PlateObject[];
  selected: Selected | null;
  gizmo: Gizmo;
  uniformScale: boolean;
  contextMenu: { x: number; y: number; object?: number; instance?: number; volume?: number } | null;
  renaming: number | null;
  meshErrors: Record<number, number>;
  clipboard: PlateObject[];
  processScope: 'global' | 'objects';
  slotColours: (string | undefined)[];
  mixes: Record<number, Mix>;
  projectMaterials: { colours: string[]; matrix: number[] | null; multiplier: number | null; builds?: { object: number; instance: number }[] } | null;
  flush: { matrix: number[] | null; multiplier: number | null };
  flushDialog: boolean;
  appDialog: 'about' | 'preferences' | 'shortcuts' | 'send' | 'setup' | 'plate' | 'clone' | 'simplify' | 'filaments' | 'objectTable' | 'createPrinter' | null;
  filamentsToUse: string[] | null;
  calibDialog: CalibMenu | null;
  printers: SendPrinter[];
  printerEdit: boolean;
  printerHosts: boolean;
  events: LayerEvent[];
  projectPlates: (ProjectPlate & { objects: PlateObject[]; events?: LayerEvent[] })[];
  plateIndex: number;
  project: File | null;
  projectName: string | null;
  job: JobView;
  noticeLog: NoticeEntry[];
  toast: string | null;
  toastSeq: number;
  toastAction: { label: string; run: () => void; msg?: string } | null;
  toastMs: number | null;
  recommendedStyle: Record<number, 'standard'>;
  heads: HeadInfo[] | null;
  presetUx: PresetUx | null;
  settingsFocus: { type: PresetType; page: string; key: string; nonce: number } | null;
  sessionDialog: 'recent' | null;
  orientPrompts: Record<number, OrientPrompt>;
  orientDismissed: number[];
  wipeTowerPos: Record<number, [number, number]>;
  wipeTower: WipeTowerEstimate | null;
  orientConfirm: OrientConfirm | null;
  settingsClipboard: { kind: 'object' | 'volume'; config: Config } | null;
}

export interface OrientPrompt {
  rotation: number[];
  pose: string;
  supportArea: [number, number];
}

export interface WipeTowerEstimate { x: number; y: number; width: number; depth: number; height: number; brim_width: number }
export interface OrientSlice { time_s: number | null; support_g: number | null; floating_mm: number | null }

export interface OrientConfirm {
  id: number;
  supportOn: boolean;
  supportArea: [number, number];
  original?: OrientSlice;
  standing?: OrientSlice;
  error?: string;
}

let state: State = {
  api: null,
  features: {},
  orientPrompts: {},
  orientDismissed: [],
  wipeTowerPos: {},
  wipeTower: null,
  orientConfirm: null,
  settingsClipboard: null,
  cut: null,
  loading: 'Connecting',
  machines: [],
  processes: [],
  filaments: [],
  userPresets: [],
  printer: '',
  process: '',
  filamentSlots: [],
  dirty: { machine: {}, process: {} },
  filamentEdits: {},
  projectPresets: null,
  projectNotice: null,
  bedType: '',
  uiMode: 'simple',
  developerMode: (() => { try { return localStorage.getItem('orca.developer_mode') === '1'; } catch { return false; } })(),
  filamentsToUse: (() => { try { return JSON.parse(localStorage.getItem('orca.web.filamentsToUse') ?? 'null'); } catch { return null; } })(),
  mixedTemperatures: (() => { try { return localStorage.getItem('orca.enable_high_low_temp_mixed_printing') === '1'; } catch { return false; } })(),
  tab: 'prepare',
  dialog: null,
  objects: [],
  selected: null,
  gizmo: 'none',
  uniformScale: true,
  contextMenu: null,
  renaming: null,
  meshErrors: {},
  clipboard: [],
  processScope: 'global',
  slotColours: [],
  mixes: {},
  projectMaterials: null,
  flush: { matrix: null, multiplier: null },
  flushDialog: false,
  appDialog: null,
  calibDialog: null,
  printers: [],
  printerEdit: false,
  printerHosts: false,
  events: [],
  projectPlates: [],
  plateIndex: 1,
  project: null,
  projectName: null,
  job: { state: 'idle', pct: 0, stage: '' },
  noticeLog: [],
  toast: null,
  toastSeq: 0,
  toastAction: null,
  toastMs: null,
  recommendedStyle: {},
  heads: null,
  presetUx: null,
  settingsFocus: null,
  sessionDialog: null,
};

const listeners = new Set<() => void>();
export const getState = () => state;
export function setState(patch: Partial<State> | ((s: State) => Partial<State>)) {
  const finishTiming = timeInteraction('store');
  let next = { ...state, ...(typeof patch === 'function' ? patch(state) : patch) };
  if (next.userPresets !== state.userPresets) {
    const repaired = preserveProfileParents(state, next);
    repaired.forEach((p, i) => { if (p !== next.userPresets[i]) rememberProfileUpdate(p, next.api?.mock); });
    next.userPresets = repaired;
  }
  if (next.machines !== state.machines || next.processes !== state.processes || next.filaments !== state.filaments || next.userPresets !== state.userPresets
    || next.printer !== state.printer || next.process !== state.process || next.filamentSlots !== state.filamentSlots) next = ensureDefaultProfiles(next);
  if (next.userPresets !== state.userPresets || next.machines !== state.machines || next.processes !== state.processes || next.filaments !== state.filaments) {
    next.userPresets = next.userPresets.map((p) => {
      if (p.base && p.overlay && p.baseline) return p;
      const migrated = prepareProfile(next, p);
      if (migrated.base) rememberProfile(migrated, p, next.api?.mock, true);
      return migrated;
    });
  }
  const selected = next.selected;
  if (selected && !next.objects.some((o) => o.id === selected.object && o.instances.some((i) => i.id === selected.instance))) {
    next.selected = null;
    next.gizmo = 'none';
  } else if (selected?.volume !== undefined && !next.objects.find((o) => o.id === selected.object)?.volumes?.some((v) => v.id === selected.volume)) {
    next.selected = { ...selected, volume: undefined, allInstances: true };
  }
  if (next.selected !== tools.get().context.selected || next.gizmo !== tools.get().context.gizmo)
    tools.send({ type: 'SYNC', selected: next.selected, gizmo: next.gizmo });
  if (next.job.plateSignature && !next.job.stale && next.job.plateSignature !== plateSignature(next))
    next.job = { ...next.job, stale: true };
  state = next;
  if ((app.get().value === 'empty' && next.objects.length) || (app.get().value === 'ready' && !next.objects.length))
    app.send({ type: 'DOCUMENT', hasObjects: next.objects.length > 0 });
  listeners.forEach((l) => l());
  finishTiming();
}
const subscribe = (l: () => void) => {
  listeners.add(l);
  return () => listeners.delete(l);
};
export const subscribeStore = subscribe;
export function useStore<T>(sel: (s: State) => T): T {
  return useSyncExternalStore(subscribe, () => sel(state));
}

export const effectiveMode = (s: State): OptionMode => s.developerMode ? 'developer' : s.uiMode;

let nextId = 1;
export const newId = () => nextId++;
export const reserveIds = (max: number) => { nextId = Math.max(nextId, max + 1); };

export const DEMO_MODELS = ['Bambu Lab A1 mini', 'Bambu Lab A1', 'Bambu Lab P1S', 'Prusa MINI', 'Prusa MINI IS', 'Prodcan3 5-axis BC table', 'Fairino FR3 cell'];
export const DEFAULT_PRINTER = 'Bambu Lab A1 mini 0.4 nozzle';

export function listFor(s: State, type: PresetType): SystemPreset[] {
  return type === 'machine' ? s.machines : type === 'process' ? s.processes : s.filaments;
}

const resolvedCatalogs = new WeakMap<SystemPreset[], WeakMap<UserPreset[], Map<string, ReturnType<typeof resolveProfile>>>>();
export function resolvePreset(s: Parameters<typeof resolveProfile>[0], type: PresetType, name: string) {
  const catalog = type === 'machine' ? s.machines : type === 'process' ? s.processes : s.filaments;
  let users = resolvedCatalogs.get(catalog);
  if (!users) resolvedCatalogs.set(catalog, (users = new WeakMap()));
  let values = users.get(s.userPresets);
  if (!values) users.set(s.userPresets, (values = new Map()));
  const key = `${type}:${name}`;
  let result = values.get(key);
  if (!result) values.set(key, (result = resolveProfile(s, type, name)));
  return result;
}

const profileConflictRows = new WeakMap<State, Map<string, ReturnType<typeof profileConflicts>>>();
export function selectProfileConflicts(s: State, type: PresetType, name?: string) {
  let cache = profileConflictRows.get(s);
  if (!cache) profileConflictRows.set(s, (cache = new Map()));
  const selected = name ?? (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[0] ?? '');
  const key = `${type}\0${selected}`;
  if (!cache.has(key)) cache.set(key, profileConflicts(s, type, selected));
  return cache.get(key)!;
}

export async function resolveProfileConflict(type: PresetType, name: string, key: string, choice: 'mine' | 'theirs') {
  const s = getState(), preset = decideConflict(s, type, name, key, choice);
  if (!preset || !s.api) return;
  await s.api.saveUserPreset(preset);
  setState((st) => ({ userPresets: st.userPresets.map((p) => p.type === type && p.name === name ? preset : p), job: markStale(st.job) }));
}

const effectiveValues = new WeakMap<Config, Map<string, { project: Config | undefined; plate: Config | undefined; edits: Config; value: Config }>>();
export function effectiveConfig(s: State, type: PresetType, slot = 0): Config {
  const name = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const { config } = resolvePreset(s, type, name ?? '');
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const plate = type === 'process' ? s.projectPlates.find((p) => p.index === s.plateIndex)?.settings : undefined;
  const edits = presetEdits(s, type, slot), key = `${type}:${slot}`;
  let values = effectiveValues.get(config);
  if (!values) effectiveValues.set(config, (values = new Map()));
  const cached = values.get(key);
  if (cached && cached.project === project && cached.plate === plate && cached.edits === edits) return cached.value;
  const value = { ...config, ...project, ...plate, ...edits };
  values.set(key, { project, plate, edits, value });
  return value;
}

export function compatible(s: State, type: 'process' | 'filament', printer = s.printer, process = s.process): SystemPreset[] {
  const print = type === 'filament' ? resolvePreset(s, 'process', process).base?.name ?? process : undefined;
  return listFor(s, type).filter((p) => isCompatible(p, printer, print));
}

const printerCache = new WeakMap<SystemPreset, WeakMap<Config, SystemPreset>>();
export function printerPreset(s: State): SystemPreset | undefined {
  const { base } = resolvePreset(s, 'machine', s.printer);
  if (!base) return undefined;
  const config = effectiveConfig(s, 'machine');
  let configs = printerCache.get(base);
  if (!configs) printerCache.set(base, (configs = new WeakMap()));
  const cached = configs.get(config);
  if (cached) return cached;
  const preset = { ...base, config };
  configs.set(config, preset);
  return preset;
}

export function filamentVariant(s: State, name: string, printer = s.printer, process = s.process): string | undefined {
  const { base } = resolvePreset(s, 'filament', name);
  const machine = resolvePreset(s, 'machine', printer).base?.name ?? printer;
  const print = resolvePreset(s, 'process', process).base?.name ?? process;
  if (base && isCompatible(base, machine, print)) return name;
  const identity = (n: string) => n.replace(/\s*@.*$/, '').trim();
  const variants = compatible(s, 'filament', machine, process);
  return (variants.find((p) => identity(p.name) === identity(base?.name ?? name))
    ?? variants.find((p) => !!base?.filament_id && p.filament_id === base.filament_id
      && String(p.config.filament_vendor) === String(base.config.filament_vendor)
      && String(p.config.filament_type) === String(base.config.filament_type)))?.name;
}

export function filamentSwitchReason(s: State, slot: number): string | null {
  const name = s.filamentSlots[slot];
  return name && !filamentVariant(s, name) ? 'Unavailable for this machine' : null;
}

export function selectPrinter(name: string) {
  setState((s) => {
    const next = { ...s, printer: name };
    const base = resolvePreset(next, 'machine', name).base;
    const procs = compatible(next, 'process', base?.name ?? name);
    const defProcess = String(base?.config.default_print_profile ?? '');
    const currentProcess = resolvePreset(s, 'process', s.process).base?.name ?? s.process;
    const process = procs.some((p) => p.name === currentProcess)
      ? s.process
      : (procs.find((p) => p.name === defProcess)?.name ?? procs[0]?.name ?? '');
    const fils = compatible(next, 'filament', base?.name ?? name, process);
    const defFilaments = base?.config.default_filament_profile;
    const preferred =
      (Array.isArray(defFilaments) ? defFilaments : [defFilaments]).map(String).find((m) => fils.some((f) => f.name === m)) ??
      base?.default_materials?.find((m) => fils.some((f) => f.name === m) && /PLA Basic/.test(m)) ??
      base?.default_materials?.find((m) => fils.some((f) => f.name === m)) ??
      fils.find((f) => /Generic PLA/.test(f.name))?.name ??
      fils[0]?.name ??
      '';
    const filamentSlots = (s.filamentSlots.length ? s.filamentSlots : ['']).map((f) =>
      f ? filamentVariant(next, f, name, process) ?? f : preferred,
    );
    return {
      printer: name,
      projectPresets: null,
      process,
      filamentSlots,
      bedType: base?.default_bed_type || String(base?.config.curr_bed_type ?? ''),
      dirty: { machine: {}, process: {} },
      filamentEdits: {},
      job: markStale(s.job),
    };
  });
}

const markStale = (job: JobView): JobView => (job.state === 'done' ? { ...job, stale: true } : job);

const EMPTY_EDITS: Config = {};
export function presetEdits(s: State, type: PresetType, slot = 0): Config {
  return type === 'filament' ? s.filamentEdits[s.filamentSlots[slot]] ?? EMPTY_EDITS : s.dirty[type];
}

const settingName = (key: string) => `Change setting "${OPTIONS[key]?.label || key}"`;

export function setOption(type: PresetType, key: string, value: ConfigValue, slot = 0) {
  undoable(settingName(key), () => setState((s) => {
    const edits = { ...presetEdits(s, type, slot), [key]: value };
    return {
      ...(type === 'filament' ? { filamentEdits: { ...s.filamentEdits, [s.filamentSlots[slot]]: edits } }
        : { dirty: { ...s.dirty, [type]: edits } }),
      job: markStale(s.job),
    };
  }), `${type}:${slot}:${key}`);
}

export function resetOption(type: PresetType, key: string, slot = 0) {
  undoable(`Reset setting "${OPTIONS[key]?.label || key}"`, () => setState((s) => {
    const edits = { ...presetEdits(s, type, slot) };
    delete edits[key];
    return {
      ...(type === 'filament' ? { filamentEdits: { ...s.filamentEdits, [s.filamentSlots[slot]]: edits } }
        : { dirty: { ...s.dirty, [type]: edits } }),
      job: markStale(s.job),
    };
  }));
}

export function hasUnsavedEdits(s: State, type: PresetType, slot: number) {
  if (!selectChanges(s, type, slot).some((c) => c.scope === 'project' && c.owner.plate === undefined)) return false;
  return type !== 'filament' || !s.filamentSlots.some((f, i) => i !== slot && f === s.filamentSlots[slot]);
}

export async function saveUserPreset(type: PresetType, name: string, slot = 0, _detach = false) {
  const s = getState();
  const current = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const values = { ...resolvePreset(s, type, current).config, ...project, ...presetEdits(s, type, slot) };
  const preset = saveProfile(s, type, name, current, values);
  await s.api!.saveUserPreset(preset);
  setState((st) => {
    const patch: Partial<State> = {
      userPresets: [...st.userPresets.filter((u) => !(u.type === type && u.name === name)), preset],
    };
    if (type === 'filament') {
      const filamentEdits = { ...st.filamentEdits };
      delete filamentEdits[current];
      patch.filamentEdits = filamentEdits;
      patch.filamentSlots = st.filamentSlots.map((f) => f === current ? name : f);
      if (st.projectPresets) patch.projectPresets = { ...st.projectPresets,
        filaments: st.projectPresets.filaments.map((c, i) => i === slot ? {}
          : st.filamentSlots[i] === current ? { ...c, ...presetEdits(s, type, slot) } : c) };
    } else {
      patch.dirty = { ...st.dirty, [type]: {} };
      if (type === 'machine') patch.printer = name;
      else patch.process = name;
      if (st.projectPresets) patch.projectPresets = { ...st.projectPresets, [type]: {} };
    }
    return patch;
  });
}

export function logNotice(message: string, kind: NoticeKind = noticeKind(message)) {
  setState((s) => ({ noticeLog: [...s.noticeLog.slice(-99), { id: (s.noticeLog.at(-1)?.id ?? 0) + 1, message, kind }] }));
}

export function toast(msg: string, action: State['toastAction'] = null, ms = action ? 8000 : null) {
  logNotice(msg);
  if (shouldToast(msg, action))
    setState({ toast: msg, toastSeq: getState().toastSeq + 1, toastAction: action && { ...action, msg }, toastMs: ms });
}

function editName(patch: object): string {
  const k = Object.keys(patch);
  return k.includes('scale') ? 'Scale' : k.some((x) => x.startsWith('rot')) ? 'Rotate' : k.some((x) => 'xyz'.includes(x)) ? 'Move'
    : k.includes('extruder') ? 'Change Filament' : k.includes('name') ? 'Rename' : 'Edit object';
}

export function setPaint(id: number, kind: PaintKind, paint: Painting | null, name: string, volume?: number) {
  const painted = <T extends { paint?: Partial<Record<PaintKind, Painting>> }>(x: T): T => ({ ...x, paint: { ...x.paint, [kind]: paint ?? undefined } });
  undoable(name, () => setState((s) => ({
    objects: s.objects.map((o) => (o.id !== id ? o : volume === undefined ? painted(o)
      : { ...o, volumes: o.volumes?.map((v) => (v.id === volume ? painted(v) : v)) })),
    job: markStale(s.job),
  })));
}

export function mapPaint(id: number, kind: PaintKind, name: string, f: (target: { geometry: THREE.BufferGeometry; paint?: Partial<Record<PaintKind, Painting>> }, scale: number) => Painting | null) {
  undoable(name, () => setState((s) => ({
    objects: s.objects.map((o) => {
      if (o.id !== id) return o;
      const scale = new THREE.Matrix4().compose(new THREE.Vector3(), new THREE.Quaternion(), new THREE.Vector3(...o.scale)).getMaxScaleOnAxis();
      const next = <T extends { geometry: THREE.BufferGeometry; paint?: Partial<Record<PaintKind, Painting>> }>(x: T): T => {
        const p = f(x, scale);
        return p === (x.paint?.[kind] ?? null) ? x : { ...x, paint: { ...x.paint, [kind]: p ?? undefined } };
      };
      return { ...next(o), volumes: o.volumes?.map((v) => (v.type === 'part' ? next(v) : v)) };
    }),
    job: markStale(s.job),
  })));
}

export function updateObject(id: number, patch: Partial<PlateObject>, name = editName(patch)) {
  undoable(name, () => setState((s) => ({ objects: s.objects.map((o) => (o.id === id ? { ...o, ...patch } : o)), job: markStale(s.job) })));
}

export function updateInstance(objectId: number, instanceId: number, patch: Partial<Instance>, name = editName(patch)) {
  undoable(name, () => setState((s) => ({
    objects: s.objects.map((o) =>
      o.id === objectId ? { ...o, instances: o.instances.map((i) => (i.id === instanceId ? { ...i, ...patch } : i)) } : o,
    ),
    job: markStale(s.job),
  })));
}

export type Selected = { object: number; instance: number; allInstances?: boolean; all?: boolean; picked?: string[]; volume?: number; range?: number };

export function isSelected(sel: Selected | null, object: number, instance: number) {
  if (!sel) return false;
  if (sel.all) return !sel.picked || sel.picked.includes(`${object}:${instance}`);
  return sel.object === object && (!!sel.allInstances || sel.instance === instance);
}

export const selectedObjectIds = (s: State) => s.objects.filter((o) => o.instances.some((i) => isSelected(s.selected, o.id, i.id))).map((o) => o.id);

export function selectionOf(s: State, keys: string[]): Selected | null {
  const items = s.objects.flatMap((o) => o.instances.filter((i) => keys.includes(`${o.id}:${i.id}`)).map((i) => ({ o, i })));
  if (!items.length) return null;
  const [{ o, i }] = items;
  if (items.every((x) => x.o === o)) {
    if (items.length === 1) return { object: o.id, instance: i.id };
    if (items.length === o.instances.length) return { object: o.id, instance: i.id, allInstances: true };
  }
  return { object: o.id, instance: i.id, all: true, picked: items.map((x) => `${x.o.id}:${x.i.id}`) };
}

export function toggleSelection(object: number, instances: number[]) {
  const s = state, keys = instances.map((i) => `${object}:${i}`);
  const now = s.selected?.volume !== undefined ? [] : s.objects.flatMap((o) => o.instances.filter((i) => isSelected(s.selected, o.id, i.id)).map((i) => `${o.id}:${i.id}`));
  const inside = keys.every((k) => now.includes(k));
  setState({ selected: selectionOf(s, inside ? now.filter((k) => !keys.includes(k)) : [...now, ...keys.filter((k) => !now.includes(k))]) });
}

export function removeSelected() {
  if (state.selected?.all && !state.selected.picked) return deleteAll();
  if (state.selected?.volume !== undefined) return deleteVolume(state.selected.object, state.selected.volume);
  undoable('Delete selected', () => setState((s) => {
    if (!s.selected) return {};
    if (s.objects.some((o) => o.project) && !canEditProject()) return {};
    const objects = s.objects
      .map((o) => (o.instances.some((i) => isSelected(s.selected, o.id, i.id)) ? { ...o, instances: o.instances.filter((i) => !isSelected(s.selected, o.id, i.id)) } : o))
      .filter((o) => o.instances.length > 0);
    return { objects, selected: null, job: markStale(s.job) };
  }));
}

export function deleteAll() {
  const n = state.objects.length;
  undoable('Delete all', () => setState((s) => !s.project && s.projectPlates.length ? { objects: [], selected: null, job: markStale(s.job) }
    : { objects: [], selected: null, project: null, projectPlates: [], plateIndex: 1, wipeTowerPos: {}, projectName: null, projectMaterials: null, projectPresets: null, projectNotice: null, job: markStale(s.job) }));
  if (n) toast(n === 1 ? 'Deleted 1 object' : `Deleted ${n} objects`, { label: 'Undo', run: undo });
}

export function setObjectOption(id: number, key: string, value: ConfigValue) {
  undoable(settingName(key), () => setState((s) => ({
    objects: s.objects.map((o) => (o.id === id ? { ...o, settings: { ...o.settings, [key]: value } } : o)),
    job: markStale(s.job),
  })), `object:${id}:${key}`);
}

export function removeObjectOptions(id: number, keys: string[]) {
  undoable('Delete Settings', () => setState((s) => ({
    objects: s.objects.map((o) => {
      if (o.id !== id) return o;
      const settings = { ...o.settings };
      for (const k of keys) delete settings[k];
      return { ...o, settings };
    }),
    job: markStale(s.job),
  })));
}

export function setEvents(events: LayerEvent[], name = 'Edit layer events') {
  undoable(name, () => setState((s) => ({ events: [...events].sort((a, b) => a.z - b.z), job: markStale(s.job) })));
}

export { markStale };

export function togglePrintable(objectId: number, instanceId?: number) {
  const o = state.objects.find((o) => o.id === objectId);
  if (!o) return;
  const printable = (o.instances.find((i) => i.id === instanceId) ?? o.instances[0]).printable === false;
  updateObject(objectId, { instances: o.instances.map((i) => instanceId === undefined || i.id === instanceId ? { ...i, printable } : i) },
    printable ? 'Set Printable' : 'Set Unprintable');
}

const mapVolumes = (s: State, objectId: number, f: (v: Volume[]) => Volume[]) =>
  s.objects.map((o) => (o.id === objectId ? { ...o, volumes: f(o.volumes ?? []) } : o));

export function updateVolume(objectId: number, volumeId: number, patch: Partial<Volume>, name = 'matrix' in patch ? 'Move' : editName(patch)) {
  undoable(name, () => setState((s) => ({
    objects: mapVolumes(s, objectId, (vs) => vs.map((v) => (v.id === volumeId ? { ...v, ...patch } : v))),
    job: markStale(s.job),
  })));
}

export function addVolume(objectId: number, volume: Volume, name: string) {
  undoable(name, () => setState((s) => {
    const o = s.objects.find((ob) => ob.id === objectId);
    if (!o) return {};
    const volumes = sortVolumes([...(o.volumes ?? []), volume]);
    return { objects: mapVolumes(s, objectId, () => volumes), selected: { object: objectId, instance: o.instances[0].id, volume: volume.id }, job: markStale(s.job) };
  }));
}

const sortVolumes = (vs: Volume[]) => vs.sort((a, b) => VOLUME_ORDER.indexOf(a.type) - VOLUME_ORDER.indexOf(b.type));

export function changeVolumeType(objectId: number, volumeId: number, type: VolumeType) {
  undoable('Change part type', () => setState((s) => ({
    objects: mapVolumes(s, objectId, (vs) => sortVolumes(vs.map((v) => (v.id === volumeId ? { ...v, type } : v)))),
    job: markStale(s.job),
  })));
}
export const VOLUME_ORDER: VolumeType[] = ['part', 'negative', 'modifier', 'support_blocker', 'support_enforcer', 'seam'];

export function deleteVolume(objectId: number, volumeId: number) {
  undoable('Delete part', () => setState((s) => ({
    objects: mapVolumes(s, objectId, (vs) => vs.filter((v) => v.id !== volumeId)),
    selected: s.selected?.volume === volumeId ? { object: objectId, instance: s.selected.instance, allInstances: true } : s.selected,
    job: markStale(s.job),
  })));
}

export function setVolumeOption(objectId: number, volumeId: number, key: string, value: ConfigValue) {
  undoable(settingName(key), () => setState((s) => ({
    objects: mapVolumes(s, objectId, (vs) => vs.map((v) => (v.id === volumeId ? { ...v, settings: { ...v.settings, [key]: value } } : v))),
    job: markStale(s.job),
  })), `volume:${volumeId}:${key}`);
}

export function removeVolumeOptions(objectId: number, volumeId: number, keys: string[]) {
  undoable('Delete Settings', () => setState((s) => ({
    objects: mapVolumes(s, objectId, (vs) => vs.map((v) => {
      if (v.id !== volumeId) return v;
      const settings = { ...v.settings };
      for (const k of keys) delete settings[k];
      return { ...v, settings };
    })),
    job: markStale(s.job),
  })));
}
