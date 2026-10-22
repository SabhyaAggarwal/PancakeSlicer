import { useSyncExternalStore } from 'react';
import { plateSignature } from '../slice-input';
import * as THREE from 'three';
import type { Api } from '../api/client';
import type { Config, ConfigValue, JobStatus, PresetType, SystemPreset, UserPreset } from '../api/types';
import type { ProjectPlate } from '../plater/threemf';
import { isHidden, OPTIONS, type OptionMode } from '../options';
import { undoable } from './history';
import { canEditProject, canOneShot } from '../api/client';
import type { PaintKind, Painting } from '../plater/paint';


export interface Instance {
  id: number;
  x: number;
  y: number;
  rotZ: number;
  z?: number;
  printable?: boolean;
}

export type VolumeType = 'part' | 'negative' | 'modifier' | 'support_blocker' | 'support_enforcer';

export interface Volume {
  id: number;
  name: string;
  type: VolumeType;
  geometry: THREE.BufferGeometry;
  matrix: number[];
  extruder: number;
  settings: Config;
}

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
  settings: Config;
  volumes?: Volume[];
  paint?: Partial<Record<PaintKind, Painting>>;
  layerProfile?: number[];
  project?: { buildIndex?: number; plateIndex?: number; plateOrigin?: [number, number]; file: string; placeholder: boolean; missing?: boolean; origin?: [number, number, number];
    saved?: { object: number; instance: number; instanceId: number; extruder: number; rotZ: number; item?: number[] } };
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
}

export type PresetUx =
  | { kind: 'search' }
  | { kind: 'save'; type: PresetType; slot: number; then?: string }
  | { kind: 'unsaved'; type: PresetType; slot: number; target: string }
  | { kind: 'compare'; type: PresetType; left?: string; right?: string }
  | { kind: 'export' }
  | { kind: 'import' };

export type MainTab = 'prepare' | 'preview';
export type CalibMenu = 'temperature' | 'max-flowrate' | 'pressure-advance' | 'flow-ratio' | 'retraction' | 'cornering' | 'input-shaping-freq' | 'input-shaping-damp' | 'vfa';
export type Gizmo = 'none' | 'move' | 'rotate' | 'scale' | 'flatten' | 'cut' | 'measure' | 'layers' | PaintKind;

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
  exported?: string;
}

export interface CutPlane {
  z: number;
  rotX: number;
  rotY: number;
  keepUpper: boolean;
  keepLower: boolean;
  placeLower: boolean;
}

export interface State {
  api: Api | null;
  features: { cut?: boolean; volumes?: boolean; edit?: boolean; mesh_plates?: boolean; paint?: boolean; layer_profile?: boolean; arrange?: boolean; project_volumes?: boolean };
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
  tab: MainTab;
  dialog: { type: 'machine' | 'filament'; slot: number } | null;
  objects: PlateObject[];
  selected: { object: number; instance: number; allInstances?: boolean; all?: boolean; volume?: number } | null;
  gizmo: Gizmo;
  uniformScale: boolean;
  contextMenu: { x: number; y: number; object?: number; instance?: number; volume?: number } | null;
  renaming: number | null;
  clipboard: PlateObject[];
  processScope: 'global' | 'objects';
  slotColours: (string | undefined)[];
  projectMaterials: { colours: string[]; matrix: number[] | null; multiplier: number | null; builds?: { object: number; instance: number }[] } | null;
  flush: { matrix: number[] | null; multiplier: number | null };
  flushDialog: boolean;
  appDialog: 'about' | 'preferences' | 'shortcuts' | 'send' | 'setup' | 'what' | null;
  calibDialog: CalibMenu | null;
  printers: { name: string; kind: string }[];
  events: LayerEvent[];
  projectPlates: (ProjectPlate & { objects: PlateObject[]; events?: LayerEvent[] })[];
  plateIndex: number;
  project: File | null;
  projectName: string | null;
  job: JobView;
  toast: string | null;
  recommendedStyle: Record<number, 'standard'>;
  heads: HeadInfo[] | null;
  presetUx: PresetUx | null;
  settingsFocus: { type: PresetType; page: string; key: string; nonce: number } | null;
  sessionDialog: 'restore' | 'recent' | null;
}

let state: State = {
  api: null,
  features: {},
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
  tab: 'prepare',
  dialog: null,
  objects: [],
  selected: null,
  gizmo: 'none',
  uniformScale: true,
  contextMenu: null,
  renaming: null,
  clipboard: [],
  processScope: 'global',
  slotColours: [],
  projectMaterials: null,
  flush: { matrix: null, multiplier: null },
  flushDialog: false,
  appDialog: null,
  calibDialog: null,
  printers: [],
  events: [],
  projectPlates: [],
  plateIndex: 1,
  project: null,
  projectName: null,
  job: { state: 'idle', pct: 0, stage: '' },
  toast: null,
  recommendedStyle: {},
  heads: null,
  presetUx: null,
  settingsFocus: null,
  sessionDialog: null,
};

const listeners = new Set<() => void>();
export const getState = () => state;
export function setState(patch: Partial<State> | ((s: State) => Partial<State>)) {
  const next = { ...state, ...(typeof patch === 'function' ? patch(state) : patch) };
  if (next.job.plateSignature && !next.job.stale && next.job.plateSignature !== plateSignature(next))
    next.job = { ...next.job, stale: true };
  state = next;
  listeners.forEach((l) => l());
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

export const DEMO_MODELS = ['Bambu Lab A1 mini', 'Bambu Lab A1', 'Bambu Lab P1S', 'Prusa MINI', 'Prusa MINI IS', 'Prodcan3 5-axis BC table'];
export const DEFAULT_PRINTER = 'Bambu Lab A1 mini 0.4 nozzle';

export function listFor(s: State, type: PresetType): SystemPreset[] {
  return type === 'machine' ? s.machines : type === 'process' ? s.processes : s.filaments;
}

export function resolvePreset(s: State, type: PresetType, name: string): { base?: SystemPreset; user?: UserPreset; config: Config } {
  const user = s.userPresets.find((u) => u.type === type && u.name === name);
  const base = listFor(s, type).find((p) => p.name === (user ? user.inherits : name));
  return { base, user, config: { ...(base?.config ?? {}), ...(user?.overrides ?? {}) } };
}

export function effectiveConfig(s: State, type: PresetType, slot = 0): Config {
  const name = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const { config } = resolvePreset(s, type, name ?? '');
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const plate = type === 'process' ? s.projectPlates.find((p) => p.index === s.plateIndex)?.settings : undefined;
  const out = { ...config, ...project, ...plate, ...presetEdits(s, type, slot) };
  if (out.nonplanar_mode === 'oneshot' && !canOneShot()) out.nonplanar_mode = 'auto';
  return out;
}

export function isCompatible(p: SystemPreset, printer: string, process?: string): boolean {
  if (!p.compatible_evaluated) return !p.compatible_printers?.length || p.compatible_printers.includes(printer);
  return !!p.compatible_printers?.includes(printer) && (!process || !p.compatible_prints || p.compatible_prints.includes(process));
}

export function compatible(s: State, type: 'process' | 'filament', printer = s.printer, process = s.process): SystemPreset[] {
  const print = type === 'filament' ? resolvePreset(s, 'process', process).base?.name ?? process : undefined;
  return listFor(s, type).filter((p) => isCompatible(p, printer, print));
}

const printerCache = new WeakMap<State, SystemPreset>();
export function printerPreset(s: State): SystemPreset | undefined {
  if (printerCache.has(s)) return printerCache.get(s);
  const { base } = resolvePreset(s, 'machine', s.printer);
  if (!base) return undefined;
  const preset = { ...base, config: effectiveConfig(s, 'machine') };
  printerCache.set(s, preset);
  return preset;
}

export function selectPrinter(name: string) {
  setState((s) => {
    const next = { ...s, printer: name };
    const base = resolvePreset(next, 'machine', name).base;
    const procs = compatible(next, 'process', base?.name ?? name);
    const defProcess = String(base?.config.default_print_profile ?? '');
    const process = procs.some((p) => p.name === s.process)
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
      fils.some((x) => x.name === f) ? f : preferred,
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
  if (!Object.keys(presetEdits(s, type, slot)).length) return false;
  return type !== 'filament' || !s.filamentSlots.some((f, i) => i !== slot && f === s.filamentSlots[slot]);
}

export const PRESET_META = /^(inherits|name|from|version|type|setting_id|base_id|filament_id|is_custom_defined|instantiation|\w+_settings_id)$/;

export async function saveUserPreset(type: PresetType, name: string, slot = 0, detach = false) {
  const s = getState();
  const current = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const { base, user } = resolvePreset(s, type, current);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const overrides = { ...(user?.overrides ?? {}), ...project, ...presetEdits(s, type, slot) };
  if (detach) for (const [key, value] of Object.entries(effectiveConfig(s, type, slot)))
    if (OPTIONS[key] && !isHidden(key) && !PRESET_META.test(key) && !(key in overrides)) overrides[key] = value;
  const preset: UserPreset = { name, type, inherits: base?.name ?? current, overrides };
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

export function toast(msg: string) {
  setState({ toast: msg });
  setTimeout(() => getState().toast === msg && setState({ toast: null }), 3500);
}

function editName(patch: object): string {
  const k = Object.keys(patch);
  return k.includes('scale') ? 'Scale' : k.some((x) => x.startsWith('rot')) ? 'Rotate' : k.some((x) => 'xyz'.includes(x)) ? 'Move'
    : k.includes('extruder') ? 'Change Filament' : k.includes('name') ? 'Rename' : 'Edit object';
}

export function setPaint(id: number, kind: PaintKind, paint: Painting | null, name: string) {
  undoable(name, () => setState((s) => ({
    objects: s.objects.map((o) => (o.id === id ? { ...o, paint: { ...o.paint, [kind]: paint ?? undefined } } : o)),
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

export function removeSelected() {
  if (state.selected?.all) return deleteAll();
  if (state.selected?.volume !== undefined) return deleteVolume(state.selected.object, state.selected.volume);
  undoable('Delete selected', () => setState((s) => {
    if (!s.selected) return {};
    if (s.objects.some((o) => o.project) && !canEditProject()) return {};
    const objects = s.objects
      .map((o) =>
        o.id === s.selected!.object ? { ...o, instances: o.instances.filter((i) => i.id !== s.selected!.instance) } : o,
      )
      .filter((o) => o.instances.length > 0);
    return { objects, selected: null, job: markStale(s.job) };
  }));
}

export function deleteAll() {
  undoable('Delete all', () => setState((s) => !s.project && s.projectPlates.length ? { objects: [], selected: null, job: markStale(s.job) }
    : { objects: [], selected: null, project: null, projectPlates: [], plateIndex: 1, projectName: null, projectMaterials: null, projectPresets: null, projectNotice: null, job: markStale(s.job) }));
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
export const VOLUME_ORDER: VolumeType[] = ['part', 'negative', 'modifier', 'support_blocker', 'support_enforcer'];

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
