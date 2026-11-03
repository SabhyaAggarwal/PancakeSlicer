import { savePreferences } from './saved-preferences';
import { findProfile } from './profiles/identity';
import { beginSlicePreview, bindSlicePreview } from './flow/preview-navigation';
import { project as projectFlow } from './flow/project';
import { app } from './flow/app';
import { importing } from './flow/import';
import { output } from './flow/output';
importing.machine.subscribe((s) => {
  if (s.value === 'running') app.send({ type: 'IMPORT' });
  else if (app.get().value !== 'booting') app.send({ type: 'SETTLED', hasObjects: getState().objects.length > 0 });
  if (s.value === 'canceled' && /^(Loading |Opening )/.test(getState().loading ?? '')) setState({ loading: null });
});
import * as THREE from 'three';
import type { Config, ConfigValue, JobRequest, JobStatus, PresetType } from './api/types';
import { sliceProgress } from './api/progress';
import { canEditProject, canLoadStep, connect, surfaceConfigBlocker, toolFeatures, type Api } from './api/client';
import { validateProfileSubmission } from './profiles/submission';
import { normalizeUserPreset } from './profiles/model';
import { shortTime } from './preview/format';
import { flushReady } from './flush-state';
import { calibOverrides, firstLayer, jobFields, outputKind, overrides, plateSignature } from './slice-input';
import { arrange, bakeStl, bedBounds, loadMesh, loadProject, nextFreeSpot, parsePoints, placeholderGeometry, plateJson, outsideBed } from './plater/geometry';
import type { ProjectItem } from './plater/threemf';
import { archivePainting } from './plater/paint';
import { projectEdits } from './plater/projectTransforms';
import { choiceDialog, confirmDialog, numberDialog, promptDialog } from './components/Ask';
import {
  PRESET_META, effectiveConfig, getState, markStale, newId, presetEdits, printerPreset, resolvePreset, selectPrinter, setState, toast, type JobView, type PlateObject, type State,
} from './state/store';
import { captureUndo, documentGeneration, historyDepth, historyVersion, leaveDocument, markSaved, newDocument, undoable } from './state/history';
import { clearPlateJobs, currentPlates } from './plater/plates';
import { nativeSupportCapable } from './settings-env';
import { loadPreviewView } from './components/preview-loader';

export async function init() {
  app.send({ type: 'BOOT' });
  const api = await connect();
  setState({ api, loading: 'Loading presets' });
  if (!api.mock) {
    setState({ features: toolFeatures });
    if (toolFeatures.orient_scores === true) void import('./plater/orientPrompt').then((m) => m.startOrientPrompt());
    if (toolFeatures.wipe_tower === true) void import('./plater/wipeTower').then((m) => m.startWipeTower());
  }
  if (!api.mock) void loadPrinters();
  await loadCatalog();
}

export async function loadCatalog() { await (await import('./profiles/catalog-startup')).loadCatalog(); }

function switchingChanges(s: State, type: PresetType, slot = 0): Config {
  const name = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const { config, user } = resolvePreset(s, type, name);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const text = (v: ConfigValue) => JSON.stringify(Array.isArray(v) ? v.map(String) : String(v));
  const baseline = Object.fromEntries(Object.entries(project ?? {}).filter(([k, v]) =>
    !PRESET_META.test(k) && (config[k] === undefined || text(v) !== text(config[k]))));
  return { ...user?.overrides, ...baseline, ...presetEdits(s, type, slot) };
}

export function switchPreset(type: PresetType, slot: number, name: string, edits: 'discard' | 'transfer' = 'transfer') {
  const before=historyVersion();
  const result=undoable(type === 'machine' ? 'Change printer' : type === 'process' ? 'Change process preset' : 'Change filament preset',
    () => applyPresetSwitch(type, slot, name, edits));
  const summary=getState().lastProfileTransition;
  if(type==='machine' && historyVersion()!==before && summary && (summary.changes.length || summary.needsSelection)){
    const undoTransition=captureUndo();
    toast(`${summary.changes.map(c=>`${c.kind==='process'?'Print settings':`Slot ${(c.slot??0)+1}`}: ${c.to ?? c.reason}`).join(' · ')}${summary.needsSelection?' · Choose compatible profiles before slicing':''}`,{label:'Undo',run:()=>{if(!undoTransition())toast('The project changed after this printer switch. Its Undo action has expired; your newer work is kept.');}});
  }
  return result;
}

function applyPresetSwitch(type: PresetType, slot: number, name: string, edits: 'discard' | 'transfer') {
  const before = getState();
  const current = type === 'machine' ? before.printer : type === 'process' ? before.process : before.filamentSlots[slot];
  let kept = 0, dropped = 0;
  if (name === current) return { kept, dropped };
  const carry = (scope: PresetType, target: string, sourceSlot = 0) => {
    if (edits === 'discard') return {};
    const config = resolvePreset(getState(), scope, target).config;
    return Object.fromEntries(Object.entries(switchingChanges(before, scope, sourceSlot)).filter(([key]) => {
      if (PRESET_META.test(key)) return false;
      if (key in config) { kept++; return true; }
      dropped++;
      return false;
    }));
  };
  if (type === 'machine') {
    selectPrinter(name);
    const selected = getState();
    if (selected.printer === name && selected.printer !== before.printer) savePreferences({'orca.printer':name,'orca.vendor':resolvePreset(selected,'machine',name).base?.vendor ?? ''});
    return {kept,dropped};
  }
  const carried = carry(type, name, slot);
  setState((st) => {
    const filamentEdits = { ...st.filamentEdits };
    let projectPresets = st.projectPresets;
    if (type === 'filament') {
      const filaments = st.filamentSlots.map((f, i) => i === slot ? carried
        : { ...st.projectPresets?.filaments[i], ...filamentEdits[f] });
      st.filamentSlots.forEach((f) => delete filamentEdits[f]);
      delete filamentEdits[name];
      projectPresets = { machine: {}, process: {}, ...projectPresets, filaments };
    } else if (projectPresets) projectPresets = { ...projectPresets, process: {} };
    return {
      dirty: type === 'process' ? { ...st.dirty, process: carried } : st.dirty,
      filamentEdits,
      projectPresets,
      ...(type === 'process' ? { process: name } : { filamentSlots: st.filamentSlots.map((f, i) => (i === slot ? name : f)) }),
      job: markStale(st.job),
    };
  });
  return { kept, dropped };
}

export function bedArea() {
  return parsePoints(printerPreset(getState())?.config.printable_area);
}

export async function importFiles(files: FileList | File[], geometryOnly = false) {
  let list = [...files];
  if (!list.length) return;
  for (const f of list) void Promise.all([import('./api/blobs'), import('./persist/session')])
    .then(([{ blobDigest }, { assignBlobKey }]) => blobDigest(f).then((sum) => assignBlobKey(f, `sha256-${sum}`))).catch(() => {});
  const choiceGeneration = documentGeneration();
  setState({ tab: 'prepare' });
  const projectFile = list.find((f) => /\.3mf$/i.test(f.name));
  const project = !!getState().project;
  const st = getState();
  const geometry = geometryOnly ? 1 : projectFile && (st.objects.length || st.projectPlates.some((p) => p.objects.length)) && (!project || canEditProject())
    ? await choiceDialog('Drop project file', `Please select an action\n${projectFile.name}`, ['Open as project', 'Import geometry only'])
    : 0;
  if (geometry === null || choiceGeneration !== documentGeneration()) return;
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
  const task = importing.begin('loading');
  const added: PlateObject[] = [];
  for (const f of list) {
    try {
      if (/\.3mf$/i.test(f.name)) {
        added.push(...await load3mfGeometry(f));
        if (!task.current()) break;
        continue;
      }
      const { geometry, source } = step.test(f.name) ? await loadStep(f, task.signal)
        : { geometry: await loadMesh(f, (pct) => { if (task.current()) { task.progress(pct, 'meshing'); setState({ loading: `Loading ${f.name} ${Math.round(pct)}%` }); } }), source: f };
      if (!task.current()) { geometry.dispose(); break; }
      if (getState().loading?.startsWith('Loading ')) setState({ loading: null });
      added.push({
        id: newId(), name: f.name, geometry, simplification: geometry.userData.simplification, rotX: 0, rotY: 0, scale: [1, 1, 1], extruder: 1, source, settings: {},
        instances: [{ id: newId(), x: 0, y: 0, rotZ: 0 }],
      });
    } catch (e) {
      if (!task.current()) break;
      task.fail(e);
      if (getState().loading?.startsWith('Loading ')) setState({ loading: null });
      toast(String(e instanceof Error ? e.message : e));
    }
  }
  if (!task.current()) { for (const o of added) o.geometry.dispose(); return; }
  if (!added.length) { task.done(); return; }
  const simplified = added.flatMap((o) => o.simplification ? [o.simplification] : []);
  if (simplified.length) void import('./simplify/ui').then((m) => m.noteSimplified(simplified));
  undoable(added.length > 1 ? 'Add objects' : 'Add object', () => setState((s) => {
    const objects = project ? added.reduce((list, o) => {
      const spot = nextFreeSpot(list, o, bedArea()) ?? bedBounds(bedArea()).center;
      return [...list, { ...o, instances: [{ ...o.instances[0], x: spot.x, y: spot.y }] }];
    }, s.objects) : arrange([...s.objects, ...added], bedArea());
    const last = added[added.length - 1];
    return { objects, selected: { object: last.id, instance: last.instances[0].id, allInstances: true }, job: s.job.state === 'done' ? { ...s.job, stale: true } : s.job };
  }));
  task.done();
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

async function loadStep(f: File, signal?: AbortSignal): Promise<{ geometry: THREE.BufferGeometry; source: File }> {
  const s = getState();
  if (!s.api || s.api.mock) throw new Error(`${f.name}: importing STEP requires the slicing service`);
  setState({ loading: `Loading ${f.name}` });
  const { vendor, printer, process, filaments } = jobFields(s, true);
  const id = await s.api.submit({ vendor, printer, process, filaments, overrides: {}, events: [], output: '3mf', files: [{ name: f.name, blob: f }] }, signal);
  if (signal?.aborted) { void s.api.cancel(id).catch(() => {}); throw new DOMException('Import canceled', 'AbortError'); }
  await new Promise<void>((resolve, reject) => {
    let unsubscribe = () => {};
    const abort = () => { unsubscribe(); void s.api!.cancel(id).catch(() => {}); reject(new DOMException('Import canceled', 'AbortError')); };
    signal?.addEventListener('abort', abort, { once: true });
    unsubscribe = s.api!.events(id, {
      progress() {},
      done() { unsubscribe(); signal?.removeEventListener('abort', abort); resolve(); },
      error(message) { unsubscribe(); signal?.removeEventListener('abort', abort); reject(new Error(`${f.name}: ${message}`)); },
    });
  });
  const response = await fetch(s.api.downloadUrl(id), { signal });
  if (!response.ok) throw new Error(`${f.name}: ${response.status} ${response.statusText}`);
  const stem = f.name.replace(/\.(step|stp|drc)$/i, '');
  let geometry: THREE.BufferGeometry | undefined;
  await loadProject(new File([await response.blob()], `${stem}.3mf`), { items() {}, mesh: (i, g) => { if (i === 0) geometry = g; }, missing() {} });
  if (!geometry) throw new Error(`${f.name}: the STEP file has no shape`);
  const mesh = { geometry, rotX: 0, rotY: 0, scale: [1, 1, 1] } as PlateObject;
  return { geometry, source: new File([await bakeStl(mesh)], `${stem}.stl`) };
}

export async function openProject(file: File) {
  const generation = documentGeneration();
  const { adoptProject, centreGeometryOnly } = await import('./plater/import-project');
  if (generation !== documentGeneration()) return;
  const s = getState();
  const offerUndo = await replacePlate('Open Project', 'Opening a project replaces the objects on the plate. Continue?');
  if (!offerUndo || generation !== documentGeneration()) return;
  leaveDocument();
  clearPlateJobs();
  const task = importing.begin('loading');
  if (s.features.explode && s.api && !s.api.mock) {
    setState({ loading: `Opening ${file.name}` });
    try {
      await (await import('./plater/explode')).openExplodedProject(file, adoptProject, task.current);
      if (!task.current()) return;
      task.done();
      newDocument(true);
      task.adoptGeneration();
      offerUndo();
      return;
    } catch (e) {
      if (!task.current()) return;
      toast(`${file.name}: ${e instanceof Error ? e.message : e}; opened as a project instead`);
    } finally {
      if (task.current()) setState({ loading: null });
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
        if (!task.current()) return;
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
          plateIndex: materials.plates[0].index, project: file, projectName: file.name, projectMaterials: materials, slotColours: materials.colours.map((c) => c || undefined), flush: { matrix: null, multiplier: null }, selected: null, processScope: 'global',
          filamentSlots: Array.from({ length: slots }, (_, i) => st.filamentSlots[i] ?? st.filamentSlots[0] ?? ''),
          job: markStale(st.job),
        }));
        task.done();
        newDocument(true);
        task.adoptGeneration();
        offerUndo();
      },
      mesh: (i, geometry, centre) => {
        if (!task.current()) { geometry.dispose(); return; }
        const painting = listed[i]?.parts === 1 || listed[i]?.partTris ? archivePainting(listed[i].paint ?? {}) : null;
        patch(i, (o) => ({ ...o, geometry, ...(painting && Object.keys(painting).length ? { paint: painting } : {}),
          project: { ...o.project!, file: file.name, placeholder: false, origin: [centre.x, centre.y, centre.z], paintable: !!painting, ...(listed[i]?.paint ? { paint: listed[i].paint } : {}), ...(painting && listed[i]?.partTris ? { partTris: listed[i].partTris } : {}) }, instances: [{ ...o.instances[0], x: centre.x, y: centre.y, z: centre.z }, ...o.instances.slice(1)] }));
      },
      missing: (i) => patch(i, (o) => ({ ...o, project: { ...o.project!, file: file.name, placeholder: true, missing: true } })),
    });
    if (!task.current()) return;
    task.done();
    if (geometryOnly && getState().project === file) centreGeometryOnly(bedArea());
    if (getState().project === file && !historyDepth().undo) markSaved();
  } catch (e) {
    if (!task.current()) return;
    task.fail(e);
    toast(`${file.name}: ${e instanceof Error ? e.message : e}`);
  }
}

export function clearPlate() {
  clearPlateJobs();
  setState((st) => ({ objects: [], selected: null, gizmo: 'none', project: null, projectPlates: [], plateIndex: 1, wipeTowerPos: {}, projectName: null, projectMaterials: null, projectPresets: null, projectNotice: null, events: [], flush: { matrix: null, multiplier: null }, processScope: 'global', job: markStale(st.job) }));
}

let creatingProject:Promise<boolean>|null=null;
export function newProject():Promise<boolean> {
  if(creatingProject)return creatingProject;
  creatingProject=(async()=>{
    const offerUndo=await replacePlate('New Project','Start a new project? The objects on the plate are removed.');
    if(!offerUndo)return false;
    leaveDocument();clearPlate();newDocument(true);offerUndo();return true;
  })().finally(()=>{creatingProject=null;});
  return creatingProject;
}

async function replacePlate(title: string, question: string): Promise<(() => void) | null> {
  const s = getState();
  const projects = await import('./flow/project');
  if (projects.storing()) { try { await projects.flushProject(); } catch { return null; } }
  if (!s.objects.length && !s.projectPlates.some((p) => p.objects.length)) return () => {};
  if (!projects.storing()) return (await confirmDialog(title, question)) ? () => {} : null;
  if (title === 'New Project') return () => {};
  return () => toast('Opened', { label: 'Undo', run: () => { const id = projects.previousProject(); if (id) void projects.openProject(id); } });
}

export function openProjectDialog() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.3mf,.3MF';
  input.onchange = () => input.files?.[0] && void openProject(input.files[0]);
  input.click();
}

export function openFileDialog() { pickModelFiles(importFiles); }

export function pickModelFiles(onFiles: (files: FileList) => unknown) {
  const input = document.createElement('input');
  input.type = 'file';
  input.multiple = true;
  input.accept = ['stl', 'obj', '3mf', ...(canLoadStep() ? ['step', 'stp'] : [])].flatMap((e) => [`.${e}`, `.${e.toUpperCase()}`]).join(',');
  input.onchange = () => input.files && void onFiles(input.files);
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
  if (!saving) {
    if(s.profileLibraryLoad.state!=='ready')throw new Error(s.profileLibraryLoad.state==='failed'?s.profileLibraryLoad.error:'Wait for saved profile adoption before slicing.');
    const {flushPrinterSetups}=await import('./profiles/printer-setups');await flushPrinterSetups();
    validateProfileSubmission(s);
    const [{methodAvailability},{methodSelectionProblem}]=await Promise.all([import('./profiles/method-context'),import('./profiles/capabilities')]);
    const availability=methodAvailability(s), global=effectiveConfig(s,'process');
    const effective=[global,...s.objects.flatMap(o=>[ {...global,...o.settings},...(o.volumes ?? []).map(v=>({...global,...o.settings,...v.settings})),...(o.ranges ?? []).flatMap(r=>(o.volumes?.filter(v=>v.type === 'part') ?? [undefined]).map(v=>({...global,...o.settings,...v?.settings,...r.settings})))])];
    for(const config of effective) {const problem=methodSelectionProblem(availability,config);if(problem) throw new Error(`Slicing method unavailable: ${problem.reason}`);}
    const {readProfileLibrary}=await import('./persist/profile-library'), remote=await readProfileLibrary(s.api?.mock);
    for(const [type,key] of [['machine',s.printer],['process',s.process],...s.filamentSlots.map(key=>['filament',key])] as [PresetType,string][]) {
      const local=findProfile(s,type,key), persisted=findProfile({userPresets:remote.profiles.map(normalizeUserPreset)},type,key);
      if(!local || !persisted || (local.revision ?? 0)!==(persisted.revision ?? 0)) throw new Error('A selected profile changed in storage. Reload or save its draft before slicing.');
    }
    const configs = [effectiveConfig(s, 'process'), ...s.objects.flatMap((o) =>
      [o.settings, ...o.volumes?.map((v) => v.settings) ?? [], ...o.ranges?.map((r) => r.settings) ?? []])];
    for (const config of configs) {
      const reason = surfaceConfigBlocker(config);
      if (reason) throw new Error(reason);
    }
  }
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
    files: r.files.map((f) => `${f.name} (${f.blob.size} bytes)`),
    vendor: r.vendor, printer: r.printer, process: r.process, 'filament[]': r.filaments,
    overrides: JSON.stringify(r.overrides), plate: r.plate && JSON.stringify(r.plate), project_instances: r.project_instances,
    events: r.events.length ? JSON.stringify(r.events) : undefined, output: r.output,
  };
};

let sliceGeneration = 0;

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

export interface JobSink {
  progress(p: { pct: number; stage: string; id?: string }): void;
  done(job: JobView): void;
  failed(error: string): void;
}

export function runJob(api: Api, request: JobRequest, signature: string, sink: JobSink, calib?: string): () => void {
  let live = true, id: string | undefined, stop: (() => void) | undefined;
  const submission = new AbortController();
  void (async () => {
    try {
      id = await api.submit(request, submission.signal, (fraction) => live && sink.progress({ pct: Math.round(fraction * 100), stage: 'uploading' }));
      if (!live) return void api.cancel(id).catch(() => undefined);
      sink.progress({ id, pct: 0, stage: 'queued' });
      const warnings: string[] = [], calibLog: string[] = [];
      let pct = 0, resulted = false;
      const complete = (status: JobStatus) => sink.done({ id: id!, state: 'done', pct: 100, stage: 'done', status, plateSignature: signature, warnings: recordWarnings(status.record, warnings), ...(calib ? { calib, calibLog } : {}) });
      stop = api.events(id, {
        progress: (e) => {
          if (!live || resulted) return;
          const progress = sliceProgress(e);
          if (e.stage === 'verify' && e.pct >= 100 && request.output === 'gcode.3mf') progress.stage = 'Packaging sliced plate';
          if (progress.pct >= pct) sink.progress(progress);
          pct = Math.max(pct, progress.pct);
        },
        result: (status) => {
          if (!live || resulted) return;
          resulted = true;
          complete(status);
        },
        done: (status) => {
          if (!live) return;
          live = false;
          complete(status);
        },
        error: (msg) => {
          if (!live) return;
          live = false;
          sink.failed(msg);
        },
        log: (stream, line) => {
          if (stream === 'stdout' && line.startsWith('calib_')) calibLog.push(line);
          if (stream !== 'stderr' || !line.trim()) return;
          const m = /^warning:\s*(.*)$/i.exec(line);
          if (m) warnings.push(m[1]);
          else if (warnings.length && !/^error:/i.test(line)) warnings[warnings.length - 1] += `\n${line}`;
        },
      });
    } catch (e) {
      if (!live) return;
      live = false;
      sink.failed(String(e));
    }
  })();
  return () => {
    const wasLive = live;
    live = false;
    submission.abort();
    stop?.();
    if (wasLive && id) void api.cancel(id).catch(() => undefined);
  };
}

export async function plateJob(s = getState(), ask = true): Promise<JobRequest | string | null> {
  const outside = outsideVolumeWarning(s);
  if (outside) return outside;
  if (nativeSupportCapable(s) && (String(effectiveConfig(s, 'process').support_style) === 'native' || s.objects.some((o) => o.settings.support_style === 'native'))) {
    const conflicts = (await import('./plater/nativeSupport')).nativeSupportConflicts(s);
    if (conflicts && !ask) return conflicts;
    if (conflicts && !(await confirmDialog('Native support', `${conflicts} Slice anyway?`))) return null;
  }
  return jobRequest(s);
}

let cancelJob: (() => void) | null = null;

export async function slice(calib?: { kind: string; params: string; head?: string }) {
  await flushReady();
  const [{methodAvailability}, {methodSelectionProblem}] = await Promise.all([import('./profiles/method-context'), import('./profiles/capabilities')]);
  const s = getState();
  if (!s.api || !s.objects.length && !calib || s.job.state === 'running' || s.job.state === 'submitting') return;
  const methodProblem = methodSelectionProblem(methodAvailability(s), effectiveConfig(s,'process'));
  if(methodProblem) return void setState({job:{state:'failed',pct:0,stage:'',error:`Slicing method unavailable: ${methodProblem.reason}`}});
  const generation = ++sliceGeneration, document = documentGeneration(), projectId = projectFlow.get().context.id, plate = s.plateIndex;
  const current = () => generation === sliceGeneration && document === documentGeneration() && projectId === projectFlow.get().context.id && plate === getState().plateIndex;
  const signature = plateSignature(s);
  const { vendor, printer, process, filaments, output } = jobFields(s);
  const request = calib ? { vendor, printer, process, filaments, overrides: calibOverrides(s, calib.kind), output, events: [], files: [], calib: calib.kind, calib_params: calib.params, calib_head: calib.head }
    : await plateJob(s).catch((e: unknown) => String(e));
  if (!current() || request === null) return;
  if (typeof request === 'string') return void setState({ job: { state: 'failed', pct: 0, stage: '', error: request } });
  const navigation = beginSlicePreview(signature);
  void loadPreviewView();
  setState({ tab: 'prepare', job: { state: 'submitting', pct: 0, stage: 'uploading', plateSignature: signature, calib: calib?.kind } });
  cancelJob = runJob(s.api, request, signature, {
    progress: ({ id, ...p }) => { if (!current()) return; bindSlicePreview(navigation, id); setState(st => id ? {job: {...st.job, id, state: 'running', ...p}} : st.job.state === 'submitting' ? {job: {...st.job, ...p}} : {}); },
    done: (job) => { if (!current()) return; bindSlicePreview(navigation, job.id); setState({job: {...job, stale: plateSignature(getState()) !== signature}}); },
    failed: (error) => { if (current()) setState(st => ({job: {...st.job, state: 'failed', error}})); },
  }, calib?.kind);
}

export function cancelSlice() {
  void import('./flow/slice').then((m) => m.cancelSlice());
  ++sliceGeneration;
  cancelJob?.();
  cancelJob = null;
  setState({ job: { state: 'canceled', pct: 0, stage: '' } });
}

export function exportFilename(s = getState()): string {
  const base = s.job.calib ?? (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf|drc)$/i, '') ?? 'plate';
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
    if (Array.isArray(printers)) setState({ printers, printerEdit: r.headers.get('X-Printer-Edit') === 'true', printerHosts: !!r.headers.get('X-Printer-Hosts') });
  } catch {  }
}

function exportBlocker(s: State) {
  if (!s.objects.length) return 'Nothing to export. Add a model and slice it first.';
  const outside = outsideVolumeWarning(s);
  if (outside) return outside;
  if (s.job.state === 'running' || s.job.state === 'submitting') return 'Still slicing. Export when it finishes.';
  return s.job.state === 'done' ? 'The plate changed since the last slice. Slice again (Ctrl+R).' : 'Slice the plate first (Ctrl+R).';
}

export async function download() {
  const s = getState();
  if (!s.api || !canExport(s) || !s.job.id) return void (s.api && toast(exportBlocker(s)));
  const task = output.begin('exporting');
  let href: string;
  try {
    const r = await fetch(s.api.downloadUrl(s.job.id), { signal: task.signal });
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    href = URL.createObjectURL(await r.blob());
  } catch (e) {
    if (!task.current()) return;
    task.fail(e);
    toast(`Export failed: ${e instanceof Error ? e.message : e}`);
    return;
  }
  if (!task.current() || getState().job.id !== s.job.id || !canExport()) {
    URL.revokeObjectURL(href);
    return;
  }
  const a = document.createElement('a');
  a.href = href;
  a.download = exportFilename();
  setState((st) => ({ job: { ...st.job, exported: a.download } }));
  document.body.appendChild(a);
  a.click();
  task.done();
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

export function toggleSvg() {
  const s = getState();
  if (!s.features.emboss_svg || s.tab !== 'prepare') return;
  if (s.gizmo === 'svg') return setState({ gizmo: 'none' });
  void import('./plater/svg').then((m) => {
    if (m.editedSvg()) return setState({ gizmo: 'svg' });
    const o = s.objects.find((x) => x.id === s.selected?.object);
    if (o && (o.project?.placeholder || o.native || s.selected?.all)) return toast('SVG needs an object of the plate, or nothing selected');
    void m.addSvg(o ? o.id : null);
  });
}
