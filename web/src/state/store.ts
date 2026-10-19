import { useSyncExternalStore } from 'react';
import { plateSignature } from '../slice-input';
import * as THREE from 'three';
import type { Api } from '../api/client';
import type { Config, ConfigValue, JobStatus, PresetType, SystemPreset, UserPreset } from '../api/types';
import type { ProjectPlate } from '../plater/threemf';
import type { OptionMode } from '../options';

export interface Instance {
  id: number;
  x: number;
  y: number;
  rotZ: number;
  z?: number;
  printable?: boolean;
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
  project?: { buildIndex?: number; plateIndex?: number; plateOrigin?: [number, number]; file: string; placeholder: boolean; missing?: boolean; origin?: [number, number, number] };
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
  dirty: Record<'machine' | 'process', Config>;
  filamentEdits: Record<string, Config>;
  projectPresets: { machine: Config; process: Config; filaments: Config[] } | null;
  projectNotice: string | null;
  bedType: string;
  uiMode: OptionMode;
  tab: MainTab;
  dialog: { type: 'machine' | 'filament'; slot: number } | null;
  objects: PlateObject[];
  selected: { object: number; instance: number; allInstances?: boolean } | null;
  gizmo: Gizmo;
  processScope: 'global' | 'objects';
  slotColours: (string | undefined)[];
  projectMaterials: { colours: string[]; matrix: number[] | null; multiplier: number | null } | null;
  flush: { matrix: number[] | null; multiplier: number | null };
  flushDialog: boolean;
  events: LayerEvent[];
  projectPlates: (ProjectPlate & { objects: PlateObject[] })[];
  plateIndex: number;
  project: File | null;
  projectName: string | null;
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
  dirty: { machine: {}, process: {} },
  filamentEdits: {},
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
  projectPlates: [],
  plateIndex: 1,
  project: null,
  projectName: null,
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
  const plate = type === 'process' ? s.projectPlates.find((p) => p.index === s.plateIndex)?.settings : undefined;
  return { ...config, ...project, ...plate, ...presetEdits(s, type, slot) };
}

export function compatible(s: State, type: 'process' | 'filament', printer = s.printer): SystemPreset[] {
  return listFor(s, type).filter((p) => !p.compatible_printers?.length || p.compatible_printers.includes(printer));
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

export function setOption(type: PresetType, key: string, value: ConfigValue, slot = 0) {
  setState((s) => {
    const edits = { ...presetEdits(s, type, slot), [key]: value };
    return {
      ...(type === 'filament' ? { filamentEdits: { ...s.filamentEdits, [s.filamentSlots[slot]]: edits } }
        : { dirty: { ...s.dirty, [type]: edits } }),
      job: markStale(s.job),
    };
  });
}

export function resetOption(type: PresetType, key: string, slot = 0) {
  setState((s) => {
    const edits = { ...presetEdits(s, type, slot) };
    delete edits[key];
    return {
      ...(type === 'filament' ? { filamentEdits: { ...s.filamentEdits, [s.filamentSlots[slot]]: edits } }
        : { dirty: { ...s.dirty, [type]: edits } }),
      job: markStale(s.job),
    };
  });
}

export async function saveUserPreset(type: PresetType, name: string, slot = 0) {
  const s = getState();
  const current = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const { base, user } = resolvePreset(s, type, current);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const preset: UserPreset = {
    name, type, inherits: base?.name ?? current,
    overrides: { ...(user?.overrides ?? {}), ...project, ...presetEdits(s, type, slot) },
  };
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
  setState((s) => ({ objects: [], selected: null, project: null, projectPlates: [], plateIndex: 1, projectName: null, projectMaterials: null, projectPresets: null, projectNotice: null, job: markStale(s.job) }));
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

export function togglePrintable(objectId: number, instanceId?: number) {
  const o = state.objects.find((o) => o.id === objectId);
  if (!o) return;
  const printable = (o.instances.find((i) => i.id === instanceId) ?? o.instances[0]).printable === false;
  updateObject(objectId, { instances: o.instances.map((i) => instanceId === undefined || i.id === instanceId ? { ...i, printable } : i) });
}
