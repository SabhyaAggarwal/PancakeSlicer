import { useSyncExternalStore } from 'react';
import * as THREE from 'three';
import type { Api } from '../api/client';
import type { Config, ConfigValue, JobStatus, PresetType, SystemPreset, UserPreset } from '../api/types';
import type { OptionMode } from '../options';

export interface Instance {
  id: number;
  x: number;
  y: number;
  rotZ: number;
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
  project?: { file: string; placeholder: boolean; missing?: boolean };
}

export interface LayerEvent {
  z: number;
  type: 'pause' | 'custom' | 'template' | 'colour' | 'change';
  text?: string;
}

export type MainTab = 'prepare' | 'preview';
export type Gizmo = 'none' | 'move' | 'rotate' | 'scale';

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
  exported?: string;
}

export interface State {
  api: Api | null;
  loading: string | null;
  machines: SystemPreset[];
  processes: SystemPreset[];
  filaments: SystemPreset[];
  userPresets: UserPreset[];
  printer: string;
  process: string;
  filamentSlots: string[];
  dirty: Record<PresetType, Config>;
  projectPresets: { machine: Config; process: Config; filaments: Config[] } | null;
  projectNotice: string | null;
  bedType: string;
  uiMode: OptionMode;
  tab: MainTab;
  dialog: { type: 'machine' | 'filament'; slot: number } | null;
  objects: PlateObject[];
  selected: { object: number; instance: number } | null;
  gizmo: Gizmo;
  processScope: 'global' | 'objects';
  slotColours: (string | undefined)[];
  projectMaterials: { colours: string[]; matrix: number[] | null; multiplier: number | null } | null;
  flush: { matrix: number[] | null; multiplier: number | null };
  flushDialog: boolean;
  events: LayerEvent[];
  project: File | null;
  job: JobView;
  toast: string | null;
}

let state: State = {
  api: null,
  loading: 'Connecting',
  machines: [],
  processes: [],
  filaments: [],
  userPresets: [],
  printer: '',
  process: '',
  filamentSlots: [],
  dirty: { machine: {}, process: {}, filament: {} },
  projectPresets: null,
  projectNotice: null,
  bedType: '',
  uiMode: 'simple',
  tab: 'prepare',
  dialog: null,
  objects: [],
  selected: null,
  gizmo: 'none',
  processScope: 'global',
  slotColours: [],
  projectMaterials: null,
  flush: { matrix: null, multiplier: null },
  flushDialog: false,
  events: [],
  project: null,
  job: { state: 'idle', pct: 0, stage: '' },
  toast: null,
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

let nextId = 1;
export const newId = () => nextId++;

const fileIds = new WeakMap<File, number>();
let nextFileId = 1;
function fileId(file: File | null): number | null {
  if (!file) return null;
  if (!fileIds.has(file)) fileIds.set(file, nextFileId++);
  return fileIds.get(file)!;
}

export function plateSignature(s: State): string {
  const preset = (type: PresetType, name: string, slot = 0) => ({ name, config: effectiveConfig(s, type, slot) });
  return JSON.stringify({
    objects: s.objects.map((o) => ({
      id: o.id, name: o.name, source: fileId(o.source), geometry: o.geometry.uuid,
      rotX: o.rotX, rotY: o.rotY, scale: o.scale, extruder: o.extruder,
      instances: o.instances, settings: o.settings, project: o.project,
    })),
    project: fileId(s.project),
    printer: preset('machine', s.printer), process: preset('process', s.process),
    filaments: s.filamentSlots.map((name, slot) => preset('filament', name, slot)),
    dirty: s.dirty, bedType: s.bedType, slotColours: s.slotColours,
    projectMaterials: s.projectMaterials, flush: s.flush, events: s.events,
  }, (_key, value) => value && typeof value === 'object' && !Array.isArray(value)
    ? Object.fromEntries(Object.entries(value).sort(([a], [b]) => a.localeCompare(b))) : value);
}

export const DEMO_MODELS = ['Bambu Lab A1 mini', 'Bambu Lab A1', 'Bambu Lab P1S', 'Prusa MINI', 'Prusa MINI IS'];
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
  return { ...config, ...project, ...s.dirty[type] };
}

export function compatible(s: State, type: 'process' | 'filament', printer = s.printer): SystemPreset[] {
  return listFor(s, type).filter((p) => !p.compatible_printers?.length || p.compatible_printers.includes(printer));
}

export function printerPreset(s: State): SystemPreset | undefined {
  return resolvePreset(s, 'machine', s.printer).base;
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
    const fils = compatible(next, 'filament', base?.name ?? name);
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
      dirty: { machine: {}, process: {}, filament: {} },
      job: markStale(s.job),
    };
  });
}

const markStale = (job: JobView): JobView => (job.state === 'done' ? { ...job, stale: true } : job);

export function setOption(type: PresetType, key: string, value: ConfigValue) {
  setState((s) => ({ dirty: { ...s.dirty, [type]: { ...s.dirty[type], [key]: value } }, job: markStale(s.job) }));
}

export function resetOption(type: PresetType, key: string) {
  setState((s) => {
    const d = { ...s.dirty[type] };
    delete d[key];
    return { dirty: { ...s.dirty, [type]: d } };
  });
}

export async function saveUserPreset(type: PresetType, name: string) {
  const s = getState();
  const current = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[0];
  const { base, user } = resolvePreset(s, type, current);
  const preset: UserPreset = {
    name,
    type,
    inherits: base?.name ?? current,
    overrides: { ...(user?.overrides ?? {}), ...s.dirty[type] },
  };
  await s.api!.saveUserPreset(preset);
  const userPresets = [...s.userPresets.filter((u) => !(u.type === type && u.name === name)), preset];
  setState((st) => {
    const patch: Partial<State> = { userPresets, dirty: { ...st.dirty, [type]: {} } };
    if (type === 'machine') patch.printer = name;
    else if (type === 'process') patch.process = name;
    else patch.filamentSlots = [name, ...st.filamentSlots.slice(1)];
    return patch;
  });
}

export function toast(msg: string) {
  setState({ toast: msg });
  setTimeout(() => getState().toast === msg && setState({ toast: null }), 3500);
}

export function updateObject(id: number, patch: Partial<PlateObject>) {
  setState((s) => ({ objects: s.objects.map((o) => (o.id === id ? { ...o, ...patch } : o)), job: markStale(s.job) }));
}

export function updateInstance(objectId: number, instanceId: number, patch: Partial<Instance>) {
  setState((s) => ({
    objects: s.objects.map((o) =>
      o.id === objectId ? { ...o, instances: o.instances.map((i) => (i.id === instanceId ? { ...i, ...patch } : i)) } : o,
    ),
    job: markStale(s.job),
  }));
}

export function removeSelected() {
  setState((s) => {
    if (!s.selected) return {};
    if (s.objects.some((o) => o.project)) return {};
    const objects = s.objects
      .map((o) =>
        o.id === s.selected!.object ? { ...o, instances: o.instances.filter((i) => i.id !== s.selected!.instance) } : o,
      )
      .filter((o) => o.instances.length > 0);
    return { objects, selected: null, job: markStale(s.job) };
  });
}

export function deleteAll() {
  setState((s) => ({ objects: [], selected: null, project: null, projectMaterials: null, projectPresets: null, projectNotice: null, job: markStale(s.job) }));
}

export function setObjectOption(id: number, key: string, value: ConfigValue) {
  setState((s) => ({
    objects: s.objects.map((o) => (o.id === id ? { ...o, settings: { ...o.settings, [key]: value } } : o)),
    job: markStale(s.job),
  }));
}

export function removeObjectOptions(id: number, keys: string[]) {
  setState((s) => ({
    objects: s.objects.map((o) => {
      if (o.id !== id) return o;
      const settings = { ...o.settings };
      for (const k of keys) delete settings[k];
      return { ...o, settings };
    }),
    job: markStale(s.job),
  }));
}

export function setEvents(events: LayerEvent[]) {
  setState((s) => ({ events: [...events].sort((a, b) => a.z - b.z), job: markStale(s.job) }));
}

export { markStale };
