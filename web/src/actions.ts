import * as THREE from 'three';
import type { Config, ConfigValue, JobRequest, PresetCatalog, PresetType, SystemPreset } from './api/types';
import { sliceProgress } from './api/progress';
import { canEditProject, canLoadStep, connect } from './api/client';
import { normalizeConfig } from './config-values';
import { OPTIONS, isHidden, loadTab } from './options';
import { shortTime } from './preview/format';
import { flushReady } from './flush-state';
import { calibOverrides, firstLayer, jobFields, MIX_KEYS, outputKind, overrides, plateSignature } from './slice-input';
import { arrange, bakeStl, bedBounds, loadMesh, loadProject, nextFreeSpot, parsePoints, placeholderGeometry, plateJson, outsideBed } from './plater/geometry';
import type { ProjectItem } from './plater/threemf';
import { archivePainting } from './plater/paint';
import { projectEdits } from './plater/projectTransforms';
import { choiceDialog, confirmDialog, numberDialog, promptDialog } from './components/Ask';
import {
  DEFAULT_PRINTER, DEMO_MODELS, PRESET_META, effectiveConfig, getState, isCompatible, markStale, newId, presetEdits, printerPreset, resolvePreset, selectPrinter, setState, toast, type PlateObject, type State,
} from './state/store';
import { historyDepth, leaveDocument, markSaved, newDocument, undoable } from './state/history';
import { clearPlateJobs, currentPlates } from './plater/plates';

const VENDORS = ['BBL', 'Prusa', 'Custom'];

function mergeCatalog(vendor: string, cat: PresetCatalog) {
  const order = <T extends { vendor: string }>(l: T[]) => [...l].sort((a, b) => VENDORS.indexOf(a.vendor) - VENDORS.indexOf(b.vendor));
  setState((s) => ({
    machines: order([...s.machines.filter((p) => p.vendor !== vendor), ...cat.machines]),
    processes: order([...s.processes.filter((p) => p.vendor !== vendor), ...cat.processes]),
    filaments: order([...s.filaments.filter((p) => p.vendor !== vendor), ...cat.filaments]),
  }));
}

export async function init() {
  const api = await connect();
  setState({ api, loading: 'Loading presets' });
  if (!api.mock) void fetch('/api/healthz').then((r) => r.json()).then((h) => {
    setState({ features: h?.tool_features ?? {} });
    if (h?.tool_features?.orient_scores === true) void import('./plater/orientPrompt').then((m) => m.startOrientPrompt());
    if (h?.tool_features?.wipe_tower === true) void import('./plater/wipeTower').then((m) => m.startWipeTower());
  }).catch(() => {});
  if (!api.mock) void loadPrinters();
  const saved = localStorage.getItem('orca.printer');
  const first = localStorage.getItem('orca.vendor') ?? 'BBL';
  const vendors = [first, ...VENDORS.filter((v) => v !== first)];
  try {
    const settings = loadTab('process');
    const [cat, userPresets] = await Promise.all([
      api.presets(first, DEMO_MODELS, (c) => mergeCatalog(first, c)),
      settings.then(() => api.userPresets()).catch(() => []),
    ]);
    await settings;
    setState({ ...cat, userPresets, loading: null, filamentSlots: [] });
    const known = (n: string | null) => !!n && !!resolvePreset(getState(), 'machine', n).base;
    const printer = [saved, DEFAULT_PRINTER].find(known) ?? cat.machines[0]?.name;
    if (printer) selectPrinter(printer);
    for (const v of vendors.slice(1)) {
      mergeCatalog(v, await api.presets(v, DEMO_MODELS, (c) => mergeCatalog(v, c)));
      if (!printer && known(saved)) selectPrinter(saved!);
    }
    if (!getState().printer && getState().machines[0]) selectPrinter(getState().machines[0].name);
  } catch (e) {
    setState({ loading: null });
    toast(`Presets failed to load: ${e}`);
  }
}

export function choosePrinter(name: string) {
  selectPrinter(name);
  localStorage.setItem('orca.printer', name);
  const vendor = resolvePreset(getState(), 'machine', name).base?.vendor;
  if (vendor) localStorage.setItem('orca.vendor', vendor);
}

export function switchPreset(type: PresetType, slot: number, name: string, edits: 'discard' | 'transfer' = 'discard') {
  undoable(type === 'machine' ? 'Change printer' : type === 'process' ? 'Change process preset' : 'Change filament preset',
    () => applyPresetSwitch(type, slot, name, edits));
}

function applyPresetSwitch(type: PresetType, slot: number, name: string, edits: 'discard' | 'transfer') {
  const carried = { ...presetEdits(getState(), type, slot) };
  if (type === 'machine') {
    choosePrinter(name);
    if (edits === 'transfer') setState((s) => ({ dirty: { ...s.dirty, machine: carried } }));
    return;
  }
  setState((st) => {
    const old = st.filamentSlots[slot];
    const filamentEdits = { ...st.filamentEdits };
    if (type === 'filament' && !st.filamentSlots.some((f, i) => i !== slot && f === old)) delete filamentEdits[old];
    if (type === 'filament' && edits === 'transfer') filamentEdits[name] = { ...filamentEdits[name], ...carried };
    return {
      dirty: type === 'process' ? { ...st.dirty, process: edits === 'transfer' ? carried : {} } : st.dirty,
      filamentEdits,
      projectPresets: st.projectPresets && (type === 'process'
        ? { ...st.projectPresets, process: {} }
        : { ...st.projectPresets, filaments: st.projectPresets.filaments.map((f, i) => i === slot ? {} : f) }),
      ...(type === 'process' ? { process: name } : { filamentSlots: st.filamentSlots.map((f, i) => (i === slot ? name : f)) }),
      job: markStale(st.job),
    };
  });
}

export function bedArea() {
  return parsePoints(printerPreset(getState())?.config.printable_area);
}

export async function importFiles(files: FileList | File[], geometryOnly = false) {
  let list = [...files];
  if (!list.length) return;
  setState({ tab: 'prepare' });
  const projectFile = list.find((f) => /\.3mf$/i.test(f.name));
  const project = !!getState().project;
  const st = getState();
  const geometry = geometryOnly ? 1 : projectFile && (st.objects.length || st.projectPlates.some((p) => p.objects.length)) && (!project || canEditProject())
    ? await choiceDialog('Drop project file', `Please select an action\n${projectFile.name}`, ['Open as project', 'Import geometry only'])
    : 0;
  if (geometry === null) return;
  if (projectFile && !geometry) {
    if (list.length > 1) toast(`${projectFile.name} opens as a project; the other files were not added`);
    await openProject(projectFile);
    return;
  }
  if (project && !canEditProject()) {
    toast('A 3MF project is open; Delete all to start a new plate');
    return;
  }
  const step = /\.(step|stp|drc)$/i;
  list = list.filter((f) => /\.(stl|obj|3mf)$/i.test(f.name) || step.test(f.name) && (canLoadStep() || /\.drc$/i.test(f.name)) ||
    (toast(`${f.name} is not a model file. Choose an STL, OBJ${canLoadStep() ? ', STEP' : ''} or 3MF file.`), false));
  const added: PlateObject[] = [];
  for (const f of list) {
    try {
      if (/\.3mf$/i.test(f.name)) {
        added.push(...await load3mfGeometry(f));
        continue;
      }
      const { geometry, source } = step.test(f.name) ? await loadStep(f)
        : { geometry: await loadMesh(f, (pct) => setState({ loading: `Loading ${f.name} ${Math.round(pct)}%` })), source: f };
      if (getState().loading?.startsWith('Loading ')) setState({ loading: null });
      added.push({
        id: newId(), name: f.name, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1], extruder: 1, source, settings: {},
        instances: [{ id: newId(), x: 0, y: 0, rotZ: 0 }],
      });
    } catch (e) {
      if (getState().loading?.startsWith('Loading ')) setState({ loading: null });
      toast(String(e instanceof Error ? e.message : e));
    }
  }
  if (!added.length) return;
  undoable(added.length > 1 ? 'Add objects' : 'Add object', () => setState((s) => {
    const objects = project ? added.reduce((list, o) => {
      const spot = nextFreeSpot(list, o, bedArea()) ?? bedBounds(bedArea()).center;
      return [...list, { ...o, instances: [{ ...o.instances[0], x: spot.x, y: spot.y }] }];
    }, s.objects) : arrange([...s.objects, ...added], bedArea());
    const last = added[added.length - 1];
    return { objects, selected: { object: last.id, instance: last.instances[0].id }, job: s.job.state === 'done' ? { ...s.job, stale: true } : s.job };
  }));
}

async function load3mfGeometry(f: File): Promise<PlateObject[]> {
  const names: string[] = [], meshes: THREE.BufferGeometry[] = [];
  await loadProject(f, { items: (items) => items.forEach((it, i) => (names[i] = it.name)), mesh: (i, g) => { meshes[i] = g; }, missing() {} }, true);
  const out: PlateObject[] = [];
  for (const [i, geometry] of meshes.entries()) {
    if (!geometry) continue;
    const mesh = { geometry, rotX: 0, rotY: 0, scale: [1, 1, 1] } as PlateObject;
    const name = names[i] || f.name.replace(/\.3mf$/i, '');
    out.push({ id: newId(), name, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1], extruder: 1, settings: {},
      source: new File([await bakeStl(mesh)], `${name}.stl`), instances: [{ id: newId(), x: 0, y: 0, rotZ: 0 }] });
  }
  if (!out.length) throw new Error(`${f.name}: the project has no printable objects`);
  return out;
}

async function loadStep(f: File): Promise<{ geometry: THREE.BufferGeometry; source: File }> {
  const s = getState();
  if (!s.api || s.api.mock) throw new Error(`${f.name}: importing STEP requires the slicing service`);
  setState({ loading: `Loading ${f.name}` });
  const { vendor, printer, process, filaments } = jobFields(s, true);
  const id = await s.api.submit({ vendor, printer, process, filaments, overrides: {}, events: [], output: '3mf', files: [{ name: f.name, blob: f }] });
  await new Promise<void>((resolve, reject) => {
    const unsubscribe = s.api!.events(id, {
      progress() {},
      done() { unsubscribe(); resolve(); },
      error(message) { unsubscribe(); reject(new Error(`${f.name}: ${message}`)); },
    });
  });
  const response = await fetch(s.api.downloadUrl(id));
  if (!response.ok) throw new Error(`${f.name}: ${response.status} ${response.statusText}`);
  const stem = f.name.replace(/\.(step|stp|drc)$/i, '');
  let geometry: THREE.BufferGeometry | undefined;
  await loadProject(new File([await response.blob()], `${stem}.3mf`), { items() {}, mesh: (i, g) => { if (i === 0) geometry = g; }, missing() {} });
  if (!geometry) throw new Error(`${f.name}: the STEP file has no shape`);
  const mesh = { geometry, rotX: 0, rotY: 0, scale: [1, 1, 1] } as PlateObject;
  return { geometry, source: new File([await bakeStl(mesh)], `${stem}.stl`) };
}

function closestPreset(presets: SystemPreset[], config: Config, id: string): SystemPreset | undefined {
  const exact = presets.find((p) => p.name === id);
  if (exact) return exact;
  const text = (v: ConfigValue) => JSON.stringify(Array.isArray(v) ? v.map(String) : String(v));
  return [...presets].sort((a, b) => {
    const score = (p: SystemPreset) => Object.entries(p.config).reduce((n, [k, v]) =>
      n + (config[k] !== undefined && text(v) === text(config[k]) ? 1 : 0), 0);
    return score(b) - score(a);
  })[0];
}

function adoptProject(config: Config, slots: number, plateKeys: Set<string>) {
  const s = getState();
  const saved = normalizeConfig(config);
  const id = String(saved.printer_settings_id ?? '');
  if (!id && saved.printer_model === undefined) return setState({ projectPresets: null, projectNotice: null, mixes: {} });
  const sameModel = s.machines.filter((p) => String(p.config.printer_model) === String(saved.printer_model));
  const sameNozzle = sameModel.filter((p) => String(p.config.nozzle_diameter) === String(saved.nozzle_diameter));
  const selected = s.machines.filter((p) => p.name === (resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer));
  const printer = closestPreset(sameNozzle.length ? sameNozzle : sameModel.length ? sameModel : selected.length ? selected : s.machines, saved, id);
  if (!printer) throw new Error('No system printer is available for this project');
  selectPrinter(printer.name);
  const current = getState();
  const forPrinter = (presets: SystemPreset[]) => presets.filter((p) => isCompatible(p, printer.name));
  const process = closestPreset(forPrinter(current.processes), saved, String(saved.print_settings_id ?? ''));
  const ids = saved.filament_settings_id;
  const filamentConfigs = Array.from({ length: slots }, (_, i) => Object.fromEntries(Object.entries(saved).map(([k, v]) =>
    [k, Array.isArray(v) && current.filaments.some((p) => k in p.config) ? [v[Math.min(i, v.length - 1)] ?? ''] : v])) as Config);
  const filaments = filamentConfigs.map((c, i) => closestPreset(forPrinter(current.filaments), c, String(Array.isArray(ids) ? ids[i] ?? '' : ids ?? '')));
  const keys = (presets: SystemPreset[]) => new Set(presets.flatMap((p) => Object.keys(p.config)));
  const machineKeys = keys(current.machines), filamentKeys = keys(current.filaments);
  const take = (c: Config, accept: (k: string) => boolean) => Object.fromEntries(Object.entries(c).filter(([k]) => OPTIONS[k] && accept(k)));
  const fallback = !sameNozzle.length || !!id && !current.machines.some((p) => p.name === id);
  const text = (v: ConfigValue) => JSON.stringify(Array.isArray(v) ? v.map(String) : String(v));
  const edits = (c: Config, preset?: SystemPreset) => Object.fromEntries(Object.entries(c).filter(([k, v]) =>
    !PRESET_META.test(k) && !isHidden(k) && !plateKeys.has(k) && !MIX_KEYS.includes(k) && (preset?.config[k] === undefined || text(v) !== text(preset.config[k]))));
  const machine = fallback ? {} : take(saved, (k) => machineKeys.has(k));
  const processValues = take(saved, (k) => !machineKeys.has(k) && !filamentKeys.has(k));
  const dirty = { machine: edits(machine, printer), process: edits(processValues, process) };
  const rest = (c: Config, e: Config) => Object.fromEntries(Object.entries(c).filter(([k]) => !(k in e)));
  setState({
    process: process?.name ?? current.process,
    filamentSlots: filaments.map((p, i) => p?.name ?? current.filamentSlots[i] ?? current.filamentSlots[0]),
    projectPresets: {
      machine: rest(machine, dirty.machine),
      process: rest(processValues, dirty.process),
      filaments: filamentConfigs.map((c) => take(c, (k) => filamentKeys.has(k) && !machineKeys.has(k))),
    },
    projectNotice: fallback ? `Project printer "${id || saved.printer_model || 'unknown'}" is unavailable. Using ${printer.name}.` : null,
    bedType: String(saved.curr_bed_type ?? current.bedType),
    dirty,
    filamentEdits: {},
    mixes: {},
  });
  if ([saved.filament_is_mixed].flat().some((v) => ['1', 'true'].includes(String(v))))
    void import('./plater/mixed').then((m) => setState({ mixes: m.readMixes(saved) }));
}

function centreGeometryOnly() {
  const box = new THREE.Box3();
  for (const o of getState().objects) {
    o.geometry.computeBoundingBox();
    for (const i of o.instances) box.union(o.geometry.boundingBox!.clone().translate(new THREE.Vector3(i.x, i.y, i.z)));
  }
  const bed = bedArea(), xs = bed.map((p) => p.x), ys = bed.map((p) => p.y);
  if (box.isEmpty() || !bed.length) return;
  const dx = (Math.min(...xs) + Math.max(...xs) - box.min.x - box.max.x) / 2, dy = (Math.min(...ys) + Math.max(...ys) - box.min.y - box.max.y) / 2;
  const shift = (o: PlateObject): PlateObject => {
    const p = o.project!, [px, py] = p.plateOrigin ?? [0, 0];
    return { ...o, instances: o.instances.map((i) => ({ ...i, x: i.x + dx, y: i.y + dy })),
      project: { ...p, plateOrigin: [px - dx, py - dy], ...(p.origin ? { origin: [p.origin[0] + dx, p.origin[1] + dy, p.origin[2]] as [number, number, number] } : {}) } };
  };
  setState((st) => ({ objects: st.objects.map(shift), projectPlates: st.projectPlates.map((p) => ({ ...p, objects: p.objects.map(shift) })) }));
}

export async function openProject(file: File) {
  const s = getState();
  if ((s.objects.length || s.projectPlates.some((p) => p.objects.length)) && !(await confirmDialog('Open Project', 'Opening a project replaces the objects on the plate. Continue?'))) return;
  leaveDocument();
  clearPlateJobs();
  if (s.features.explode && s.api && !s.api.mock) {
    setState({ loading: `Opening ${file.name}` });
    try {
      await (await import('./plater/explode')).openExplodedProject(file, adoptProject);
      newDocument(true);
      return;
    } catch (e) {
      toast(`${file.name}: ${e instanceof Error ? e.message : e}; opened as a project instead`);
    } finally {
      setState({ loading: null });
    }
  }
  let objects: PlateObject[] = [];
  const patch = (i: number, f: (o: PlateObject) => PlateObject) => {
    const id = objects[i]?.id;
    if (getState().project === file) setState((st) => ({
      objects: st.objects.map((o) => (o.id === id ? f(o) : o)),
      projectPlates: st.projectPlates.map((p) => ({ ...p, objects: p.objects.map((o) => o.id === id ? f(o) : o) })),
    }));
  };
  let geometryOnly = false;
  let listed: ProjectItem[] = [];
  try {
    await loadProject(file, {
      items(items: ProjectItem[], materials) {
        listed = items;
        if (!items.length) return toast(`${file.name}: the project has no printable objects`);
        geometryOnly = !materials.hasConfig && materials.plates.length === 1;
        const slots = Math.max(1, materials.colours.length, ...items.map((i) => i.extruder));
        adoptProject(materials.config, slots, new Set(materials.plates.flatMap((p) => Object.keys(p.settings))));
        objects = items.map((it) => {
          const instanceId = newId();
          return {
            id: newId(), name: it.name, geometry: placeholderGeometry(), rotX: 0, rotY: 0, scale: [1, 1, 1],
            extruder: it.extruder, source: file, settings: it.settings, project: { file: file.name, placeholder: true, buildIndex: it.buildIndex, plateIndex: it.plateIndex, plateOrigin: materials.plates.find((p) => p.index === it.plateIndex)?.origin,
              saved: { object: Number(it.id), instance: it.instance, instanceId, extruder: it.extruder, rotZ: it.rotZ, item: it.transform } },
            instances: [{ id: instanceId, x: it.origin[0], y: it.origin[1], z: it.origin[2], rotZ: 0, printable: it.printable }],
          };
        });
        setState((st) => ({
          objects: objects.filter((o) => o.project?.plateIndex === materials.plates[0].index),
          projectPlates: materials.plates.map((p) => ({ ...p, objects: objects.filter((o) => o.project?.plateIndex === p.index) })),
          plateIndex: materials.plates[0].index, project: file, projectName: file.name, projectMaterials: materials, slotColours: [], flush: { matrix: null, multiplier: null }, selected: null, processScope: 'global',
          filamentSlots: Array.from({ length: slots }, (_, i) => st.filamentSlots[i] ?? st.filamentSlots[0] ?? ''),
          job: markStale(st.job),
        }));
        newDocument(true);
      },
      mesh: (i, geometry, centre) => {
        const painting = listed[i]?.parts === 1 || listed[i]?.partTris ? archivePainting(listed[i].paint ?? {}) : null;
        patch(i, (o) => ({ ...o, geometry, ...(painting && Object.keys(painting).length ? { paint: painting } : {}),
          project: { ...o.project!, file: file.name, placeholder: false, origin: [centre.x, centre.y, centre.z], paintable: !!painting, ...(listed[i]?.paint ? { paint: listed[i].paint } : {}), ...(painting && listed[i]?.partTris ? { partTris: listed[i].partTris } : {}) }, instances: [{ ...o.instances[0], x: centre.x, y: centre.y, z: centre.z }, ...o.instances.slice(1)] }));
      },
      missing: (i) => patch(i, (o) => ({ ...o, project: { ...o.project!, file: file.name, placeholder: true, missing: true } })),
    });
    if (geometryOnly && getState().project === file) centreGeometryOnly();
    if (getState().project === file && !historyDepth().undo) markSaved();
  } catch (e) {
    toast(`${file.name}: ${e instanceof Error ? e.message : e}`);
  }
}

export async function newProject() {
  const s = getState();
  if ((s.objects.length || s.projectPlates.some((p) => p.objects.length)) && !(await confirmDialog('New Project', 'Start a new project? The objects on the plate are removed.'))) return;
  leaveDocument();
  clearPlateJobs();
  setState((st) => ({ objects: [], selected: null, project: null, projectPlates: [], plateIndex: 1, wipeTowerPos: {}, projectName: null, projectMaterials: null, projectPresets: null, projectNotice: null, events: [], flush: { matrix: null, multiplier: null }, processScope: 'global', job: markStale(st.job) }));
  newDocument(true);
}

export function openProjectDialog() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.3mf,.3MF';
  input.onchange = () => input.files?.[0] && void openProject(input.files[0]);
  input.click();
}

export function openFileDialog() {
  const input = document.createElement('input');
  input.type = 'file';
  input.multiple = true;
  input.accept = ['stl', 'obj', '3mf', ...(canLoadStep() ? ['step', 'stp'] : [])].flatMap((e) => [`.${e}`, `.${e.toUpperCase()}`]).join(',');
  input.onchange = () => input.files && void importFiles(input.files);
  input.click();
}

export function arrangeAll(allPlates = true) {
  const s = getState();
  const locked = !!s.projectPlates.find((p) => p.index === s.plateIndex)?.locked;
  if (locked && !allPlates) return toast('The plate is locked');
  if (s.features.arrange && !s.project && s.api && !s.api.mock) return locked ? toast('The plate is locked') : void import('./plater/operations').then((m) => m.arrangeNative());
  const area = bedArea();
  undoable('Arrange', () => setState((st) => ({
    ...(locked ? {} : { objects: arrange(st.objects, area) }),
    ...(allPlates && !st.project ? { projectPlates: st.projectPlates.map((p) => (p.index === st.plateIndex || p.locked || !p.objects.length ? p : { ...p, objects: arrange(p.objects, area) })) } : {}),
  })));
}

export function addInstance() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || o.project && (o.project.placeholder || !canEditProject())) return;
  const spot = nextFreeSpot(s.objects, o, bedArea());
  const src = o.instances.find((i) => i.id === s.selected!.instance) ?? o.instances[0];
  const inst = { id: newId(), x: spot?.x ?? src.x, y: spot?.y ?? src.y, rotZ: src.rotZ, z: src.z, printable: src.printable };
  undoable('Increase Instances', () => setState((st) => {
    const objects = st.objects.map((ob) => (ob.id === o.id ? { ...ob, instances: [...ob.instances, inst] } : ob));
    return { objects: spot ? objects : arrange(objects, bedArea()), selected: { object: o.id, instance: inst.id } };
  }));
}

export function removeInstance() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || o.instances.length < 2) return;
  const keep = o.instances.slice(0, -1);
  undoable('Decrease Instances', () => setState((st) => ({
    objects: st.objects.map((ob) => (ob.id === o.id ? { ...ob, instances: keep } : ob)),
    selected: { object: o.id, instance: keep[keep.length - 1].id },
  })));
}

export async function setInstanceCount() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || o.project && (o.project.placeholder || !canEditProject())) return;
  const num = await numberDialog('Copies of the selected object', 'Number of copies:', o.instances.length, 0, 1000);
  if (num === null || num === o.instances.length) return;
  undoable(`Set numbers of copies to ${num}`, () => setState((st) => {
    const cur = st.objects.find((ob) => ob.id === o.id);
    if (!cur) return {};
    if (num === 0) return { objects: st.objects.filter((ob) => ob.id !== o.id), selected: null, job: markStale(st.job) };
    let next: PlateObject = { ...cur, instances: cur.instances.slice(0, num) };
    let full = false;
    const src = cur.instances[cur.instances.length - 1];
    while (next.instances.length < num) {
      const others = [...st.objects.filter((ob) => ob.id !== o.id), next];
      const spot: { x: number; y: number } | null = full ? null : nextFreeSpot(others, next, bedArea());
      full ||= !spot;
      next = { ...next, instances: [...next.instances, { id: newId(), x: spot?.x ?? src.x, y: spot?.y ?? src.y, rotZ: src.rotZ, z: src.z, printable: src.printable }] };
    }
    const objects = st.objects.map((ob) => (ob.id === o.id ? next : ob));
    const last = next.instances[next.instances.length - 1];
    return { objects: full ? arrange(objects, bedArea()) : objects, selected: { object: o.id, instance: last.id }, job: markStale(st.job) };
  }));
}

export function fillBed() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || o.project && (o.project.placeholder || !canEditProject())) return;
  const area = bedArea();
  const src = o.instances[o.instances.length - 1];
  const withCopies = (n: number) => arrange(s.objects.map((ob) => ob.id !== o.id ? ob : { ...ob, instances: [...ob.instances,
    ...Array.from({ length: n }, () => ({ id: newId(), x: src.x, y: src.y, rotZ: src.rotZ, z: src.z, printable: src.printable }))] }), area);
  const fits = (objects: PlateObject[]) => outsideBed(objects, area, Infinity).size === 0;
  let lo = 0, hi = 1;
  let best = withCopies(0);
  if (!fits(best)) return toast('The plate is full: no copy of the selected object fits on it');
  for (let next = withCopies(hi); hi <= 1000 && fits(next); next = withCopies(hi)) { best = next; lo = hi; hi *= 2; }
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1, next = withCopies(mid);
    if (fits(next)) { best = next; lo = mid; } else hi = mid;
  }
  if (!lo) return toast('No more copies of the selected object fit on the plate');
  undoable('Arrange', () => setState((st) => ({ objects: best, selected: { object: o.id, instance: o.instances[0].id, allInstances: true }, job: markStale(st.job) })));
}

export { outputKind } from './slice-input';
export { confirmDialog, numberDialog, promptDialog };

export async function jobRequest(s = getState(), saving = false): Promise<JobRequest> {
  await flushReady();
  const fields = jobFields(s, saving);

  if (s.project) {
    const { added, volumes } = fields.project_edits ? projectEdits(s) : { added: [], volumes: [] };
    const { files } = added.length ? await plateJson(added, firstLayer(s)) : { files: [] };
    const parts = await Promise.all(volumes.map(({ o, k }) => bakeStl({ ...o, geometry: o.volumes![k].geometry }, new THREE.Matrix4())));
    return { ...fields, files: [{ name: s.project.name, blob: s.project as Blob }, ...files, ...volumes.map(({ name }, i) => ({ name, blob: parts[i] }))] };
  }
  const locks = !!s.features.plate_locks;
  if (saving && s.features.mesh_plates && (s.projectPlates.length > 1 || s.projectPlates.some((p) => Object.keys(p.settings).length || p.name || locks && p.locked))) {
    const plates = currentPlates(s);
    const { plate, files } = await plateJson(plates.flatMap((p) => p.objects), firstLayer(s));
    plates.flatMap((p) => p.objects.map(() => p.index)).forEach((index, k) => { plate.objects[k].plate = index; });
    plate.plates = plates.map((p) => ({ index: p.index, name: p.name, settings: Object.fromEntries(Object.entries(p.settings).map(([k, v]) => [k, String(v)])), ...(locks && p.locked ? { locked: true } : {}) }));
    return { ...fields, plate, files };
  }
  const { plate, files } = await plateJson(s.objects, firstLayer(s));
  return { ...fields, plate, files };
}

(window as unknown as { __jobId: unknown }).__jobId = () => getState().job.id;
(window as unknown as { __signature: unknown }).__signature = () => plateSignature(getState());
(window as unknown as { __job: unknown }).__job = async () => {
  const r = await jobRequest();
  return {
    'files[]': r.files.map((f) => `${f.name} (${f.blob.size} bytes)`),
    vendor: r.vendor, printer: r.printer, process: r.process, 'filament[]': r.filaments,
    overrides: JSON.stringify(r.overrides), plate: r.plate && JSON.stringify(r.plate), project_instances: r.project_instances,
    events: r.events.length ? JSON.stringify(r.events) : undefined, output: r.output,
  };
};

let unsubscribe: (() => void) | null = null;
let sliceGeneration = 0;
let pendingSubmission: AbortController | null = null;

export function outsideVolumeWarning(s = getState()): string | null {
  const machine = printerPreset(s);
  if (!machine) return null;
  const objects = s.objects.filter((o) => !o.project?.placeholder);
  const outside = outsideBed(objects, parsePoints(machine.config.printable_area), Number(machine.config.printable_height ?? 1e6));
  const names = objects.filter((o) => o.instances.some((i) => outside.has(i.id))).map((o) => o.name);
  return names.length ? `Following objects are laid over the boundary of plate or exceeds the height limit:\n${names.join('\n')}\nPlease solve the problem by moving it totally on or off the plate, and confirming that the height is within the build volume.\n` : null;
}

function recordWarnings(record: Record<string, unknown> | undefined, stderr: string[]): string[] {
  const text = (record?.text_summary ?? {}) as Record<string, unknown>;
  const objects = Array.isArray(record?.object_warnings) ? record.object_warnings : [];
  const exported = typeof text.export_warning === 'string' ? text.export_warning.split('\n') : [];
  const firstLine = (w: string) => w.trim().split('\n')[0];
  const shown = new Set(stderr.map(firstLine));
  const added: string[] = [];
  for (const w of [...objects, ...exported]) {
    if (typeof w !== 'string' || !w.trim() || shown.has(firstLine(w))) continue;
    shown.add(firstLine(w));
    added.push(w.trim());
  }
  return [...stderr, ...added];
}

export async function slice(calib?: { kind: string; params: string; head?: string }) {
  await flushReady();
  const s = getState();
  if (!s.api || !s.objects.length && !calib || s.job.state === 'running' || s.job.state === 'submitting') return;
  const outside = calib ? null : outsideVolumeWarning(s);
  if (outside) {
    setState({ job: { state: 'failed', pct: 0, stage: '', error: outside } });
    return;
  }
  setState({ tab: 'preview' });
  const generation = ++sliceGeneration;
  const submission = new AbortController();
  pendingSubmission = submission;
  const signature = plateSignature(s);
  setState({ job: { state: 'submitting', pct: 0, stage: 'uploading', plateSignature: signature, calib: calib?.kind } });
  try {
    const { vendor, printer, process, filaments, output } = jobFields(s);
    const request: JobRequest = calib ? { vendor, printer, process, filaments, overrides: calibOverrides(s, calib.kind), output, events: [], files: [], calib: calib.kind, calib_params: calib.params, calib_head: calib.head }
      : await jobRequest(s);
    if (generation !== sliceGeneration) return;
    const id = await s.api.submit(request, submission.signal, (fraction) => generation === sliceGeneration &&
      setState((st) => (st.job.state === 'submitting' ? { job: { ...st.job, pct: Math.round(fraction * 100) } } : {})));
    if (generation !== sliceGeneration) {
      await s.api.cancel(id);
      return;
    }
    setState((st) => ({ job: { ...st.job, id, state: 'running', pct: 0, stage: 'queued' } }));
    unsubscribe?.();
    const warnings: string[] = [], calibLog: string[] = [];
    unsubscribe = s.api.events(id, {
      progress: (e) => {
        if (generation !== sliceGeneration) return;
        const progress = sliceProgress(e);
        if (e.stage === 'verify' && e.pct >= 100 && request.output === 'gcode.3mf') progress.stage = 'Packaging sliced plate';
        setState((st) => progress.pct < st.job.pct ? {} : { job: { ...st.job, ...progress } });
      },
      done: (status) => generation === sliceGeneration && setState((st) => ({ job: { ...st.job, id, state: 'done', pct: 100, stage: 'done', status, warnings: recordWarnings(status.record, warnings), ...(calib ? { calibLog } : {}) } })),
      error: (msg) => generation === sliceGeneration && setState((st) => ({ job: { ...st.job, state: 'failed', error: msg } })),
      log: (stream, line) => {
        if (stream === 'stdout' && line.startsWith('calib_')) calibLog.push(line);
        if (stream !== 'stderr' || !line.trim()) return;
        const m = /^warning:\s*(.*)$/i.exec(line);
        if (m) warnings.push(m[1]);
        else if (warnings.length && !/^error:/i.test(line)) warnings[warnings.length - 1] += `\n${line}`;
      },
    });
  } catch (e) {
    if (generation !== sliceGeneration) return;
    setState({ job: { state: 'failed', pct: 0, stage: '', error: String(e) } });
  }
}

export function cancelSlice() {
  ++sliceGeneration;
  pendingSubmission?.abort();
  pendingSubmission = null;
  const s = getState();
  unsubscribe?.();
  unsubscribe = null;
  if (s.job.id) void s.api?.cancel(s.job.id).catch(() => undefined);
  setState({ job: { state: 'canceled', pct: 0, stage: '' } });
}

export function exportFilename(s = getState()): string {
  const base = s.job.calib ?? (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf)$/i, '') ?? 'plate';
  const time = Number((s.job.status?.record as { text_summary?: Record<string, string> } | undefined)?.text_summary?.estimated_time_s);
  const firstTool = Math.max(0, Math.min(...s.objects.map((o) => o.extruder - 1)));
  const vars: Config = {
    ...effectiveConfig(s, 'machine'), ...effectiveConfig(s, 'filament'), ...effectiveConfig(s, 'process'), ...overrides(s),
    input_filename_base: base, initial_tool: firstTool, initial_extruder: firstTool,
    ...(Number.isFinite(time) ? { print_time: shortTime(time) } : {}),
  };
  let unknown = false;
  const at = (v: ConfigValue | undefined, i: number) => {
    const list = Array.isArray(v) ? v : typeof v === 'string' && v.includes(';') ? v.split(';') : [v];
    return list[Math.min(i, list.length - 1)];
  };
  const name = String(vars.filename_format ?? '').replace(/[{[]([a-z_]+)(?:\[([a-z_0-9]+)\])?[}\]]/g, (_, key: string, idx?: string) => {
    const v = idx === undefined ? vars[key] : at(vars[key], /^\d+$/.test(idx) ? Number(idx) : Number(vars[idx] ?? 0));
    if (v === undefined || v === '') unknown = true;
    return String(Array.isArray(v) ? v[0] : v);
  });
  const stem = !name || unknown ? base : name.replace(/\.gcode$/i, '').replace(/[<>:/\\|?*"]/g, '_');
  return `${stem}.${outputKind(s)}`;
}

export function canExport(s = getState()): boolean {
  return s.job.state === 'done' && !!s.job.id && !!s.job.plateSignature && !s.job.stale &&
    s.job.plateSignature === plateSignature(s) && !outsideVolumeWarning(s);
}

export function fileUse(machine = printerPreset(getState())) {
  const printer = machine?.model?.replace(/^Bambu Lab /, '') ?? 'printer';
  return machine?.vendor === 'BBL'
    ? `Copy it to the ${printer}'s microSD card.`
    : `Copy it to the ${printer}'s USB drive.`;
}

export const STALE_RESULT_TIP = 'Settings changed since the last slice; slice again';

export async function loadPrinters() {
  try {
    const r = await fetch('/api/printers');
    const printers = r.ok ? await r.json() : [];
    if (Array.isArray(printers)) setState({ printers, printerEdit: r.headers.get('X-Printer-Edit') === 'true' });
  } catch {  }
}

export async function download() {
  const s = getState();
  if (!s.api || !canExport(s) || !s.job.id) return;
  let href: string;
  try {
    const r = await fetch(s.api.downloadUrl(s.job.id));
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    href = URL.createObjectURL(await r.blob());
  } catch (e) {
    toast(`Export failed: ${e instanceof Error ? e.message : e}`);
    return;
  }
  if (getState().job.id !== s.job.id || !canExport()) {
    URL.revokeObjectURL(href);
    return;
  }
  const a = document.createElement('a');
  a.href = href;
  a.download = exportFilename();
  setState((st) => ({ job: { ...st.job, exported: a.download } }));
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(href), 60000);
}

export const toolbarOperation = (operation: 'orient' | 'split' | 'split-parts', all = false) =>
  import('./plater/operations').then((m) => m.run(operation, all));

export async function saveProject(as = false) {
  const saving = await import('./saveProject');
  return saving.saveProject(as);
}

export function toggleText() {
  const s = getState();
  if (!s.features.emboss_text || s.tab !== 'prepare') return;
  if (s.gizmo === 'text') return setState({ gizmo: 'none' });
  void import('./plater/text').then((m) => {
    if (m.editedText()) return setState({ gizmo: 'text' });
    const o = s.objects.find((x) => x.id === s.selected?.object);
    if (o && (o.project?.placeholder || o.native || s.selected?.all)) return toast('Text needs an object of the plate, or nothing selected');
    void m.addText(o ? o.id : null);
  });
}

export const canBoolean = (s: State) => {
  const o = s.objects.find((x) => x.id === s.selected?.object);
  return !!s.features.mesh_boolean && !!o && !s.selected?.all && !o.project?.placeholder && !o.native && !!o.volumes?.length;
};
export function toggleBoolean() {
  const s = getState();
  if (s.gizmo === 'boolean' || canBoolean(s)) setState({ gizmo: s.gizmo === 'boolean' ? 'none' : 'boolean' });
}
