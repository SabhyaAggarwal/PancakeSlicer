import type { Config, JobRequest, PresetCatalog } from './api/types';
import { connect } from './api/client';
import { currentFlush } from './components/FlushDialog';
import { eventArg } from './components/LayerEvents';
import { jobValue } from './options';
import { filamentColor } from './plater/Plater3D';
import { arrange, loadMesh, meshFromPositions, nextFreeSpot, parsePoints, placeholderGeometry, plateJson } from './plater/geometry';
import { readProject } from './plater/threemf';
import {
  DEFAULT_PRINTER, DEMO_MODELS, getState, markStale, newId, printerPreset, resolvePreset, selectPrinter, setState, toast, type PlateObject,
} from './state/store';

const VENDORS = ['BBL', 'Prusa'];

function demoPresets(cat: PresetCatalog): PresetCatalog {
  const machines = cat.machines.filter((m) => DEMO_MODELS.includes(m.model ?? String(m.config.printer_model ?? '')));
  const names = new Set(machines.map((m) => m.name));
  const forDemo = (p: { compatible_printers?: string[] }) => (p.compatible_printers ?? []).some((c) => names.has(c));
  return {
    machines,
    processes: cat.processes.filter((p) => p.name.includes('Non-planar top') && forDemo(p)),
    filaments: cat.filaments.filter(forDemo),
  };
}

export async function init() {
  const api = await connect();
  setState({ api, loading: 'Loading presets' });
  try {
    const cats = await Promise.all(VENDORS.map((v) => api.presets(v)));
    const all = demoPresets({
      machines: cats.flatMap((c) => c.machines),
      processes: cats.flatMap((c) => c.processes),
      filaments: cats.flatMap((c) => c.filaments),
    });
    const userPresets = await api.userPresets().catch(() => []);
    setState({ ...all, userPresets, loading: null, filamentSlots: [] });
    const saved = localStorage.getItem('orca.printer');
    const printer = [saved, DEFAULT_PRINTER].find((n) => n && (all.machines.some((m) => m.name === n) || userPresets.some((u) => u.name === n)));
    selectPrinter(printer ?? all.machines[0]?.name ?? '');
  } catch (e) {
    setState({ loading: null });
    toast(`Presets failed to load: ${e}`);
  }
}

export function bedArea() {
  return parsePoints(printerPreset(getState())?.config.printable_area);
}

export async function importFiles(files: FileList | File[]) {
  let list = [...files];
  if (!list.length) return;
  setState({ tab: 'prepare' });
  const projectFile = list.find((f) => /\.3mf$/i.test(f.name));
  if (projectFile) {
    if (list.length > 1) toast(`${projectFile.name} opens as a project; the other files were not added`);
    await openProject(projectFile);
    return;
  }
  if (getState().project) {
    toast('A 3MF project is open; its plate is sliced as saved. Delete all to start a new plate.');
    return;
  }
  list = list.filter((f) => /\.(stl|obj)$/i.test(f.name) || (toast(`${f.name}: STL, OBJ and 3MF are supported`), false));
  const added: PlateObject[] = [];
  for (const f of list) {
    try {
      const geometry = await loadMesh(f);
      added.push({
        id: newId(), name: f.name, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1], extruder: 1, source: f, settings: {},
        instances: [{ id: newId(), x: 0, y: 0, rotZ: 0 }],
      });
    } catch (e) {
      toast(String(e instanceof Error ? e.message : e));
    }
  }
  if (!added.length) return;
  setState((s) => {
    const objects = arrange([...s.objects, ...added], bedArea());
    const last = added[added.length - 1];
    return { objects, selected: { object: last.id, instance: last.instances[0].id }, job: s.job.state === 'done' ? { ...s.job, stale: true } : s.job };
  });
}

export async function openProject(file: File) {
  const s = getState();
  if (s.objects.length && !window.confirm('Opening a project replaces the objects on the plate. Continue?')) return;
  let project: Awaited<ReturnType<typeof readProject>>;
  try {
    project = await readProject(file);
  } catch (e) {
    toast(`${file.name}: ${e instanceof Error ? e.message : e}`);
    return;
  }
  if (!project.items.length) {
    toast(`${file.name}: the project has no printable objects`);
    return;
  }
  const slots = Math.max(s.filamentSlots.length, ...project.items.map((i) => i.extruder));
  const objects: PlateObject[] = project.items.map((it) => ({
    id: newId(), name: it.name, geometry: placeholderGeometry(), rotX: 0, rotY: 0, scale: [1, 1, 1],
    extruder: it.extruder, source: file, settings: {}, project: { file: file.name, placeholder: true },
    instances: [{ id: newId(), x: it.origin[0], y: it.origin[1], rotZ: 0 }],
  }));
  setState((st) => ({
    objects, project: file, selected: null, processScope: 'global',
    filamentSlots: st.filamentSlots.length >= slots ? st.filamentSlots : [...st.filamentSlots, ...Array(slots - st.filamentSlots.length).fill(st.filamentSlots[0] ?? '')],
    job: markStale(st.job),
  }));
  for (let i = 0; i < objects.length; i++) {
    await new Promise((r) => setTimeout(r, 0));
    if (getState().project !== file) return;
    const positions = project.mesh(i);
    const id0 = objects[i].id;
    if (!positions) {
      setState((st) => ({ objects: st.objects.map((o) => (o.id === id0 ? { ...o, project: { file: file.name, placeholder: true, missing: true } } : o)) }));
      continue;
    }
    const { geometry, centre } = meshFromPositions(positions);
    const id = objects[i].id;
    setState((st) => ({
      objects: st.objects.map((o) =>
        o.id === id
          ? { ...o, geometry, project: { file: file.name, placeholder: false }, instances: [{ ...o.instances[0], x: centre.x, y: centre.y }] }
          : o,
      ),
    }));
  }
}

export function newProject() {
  const s = getState();
  if (s.objects.length && !window.confirm('Start a new project? The objects on the plate are removed.')) return;
  setState((st) => ({ objects: [], selected: null, project: null, events: [], flush: { matrix: null, multiplier: null }, processScope: 'global', job: markStale(st.job) }));
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
  input.accept = '.stl,.obj,.3mf,.STL,.OBJ,.3MF';
  input.onchange = () => input.files && void importFiles(input.files);
  input.click();
}

export function arrangeAll() {
  if (getState().project) return;
  setState((s) => ({ objects: arrange(s.objects, bedArea()) }));
}

export function addInstance() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || o.project) return;
  const spot = nextFreeSpot(s.objects, o, bedArea());
  const src = o.instances.find((i) => i.id === s.selected!.instance) ?? o.instances[0];
  const inst = { id: newId(), x: spot?.x ?? src.x, y: spot?.y ?? src.y, rotZ: src.rotZ };
  setState((st) => {
    const objects = st.objects.map((ob) => (ob.id === o.id ? { ...ob, instances: [...ob.instances, inst] } : ob));
    return { objects: spot ? objects : arrange(objects, bedArea()), selected: { object: o.id, instance: inst.id } };
  });
}

export function removeInstance() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || o.instances.length < 2) return;
  const keep = o.instances.slice(0, -1);
  setState((st) => ({
    objects: st.objects.map((ob) => (ob.id === o.id ? { ...ob, instances: keep } : ob)),
    selected: { object: o.id, instance: keep[keep.length - 1].id },
  }));
}

export function outputKind(): 'gcode' | 'gcode.3mf' {
  const m = printerPreset(getState());
  return m?.vendor === 'BBL' ? 'gcode.3mf' : 'gcode';
}

function overrides(): Config {
  const s = getState();
  const out: Config = {};
  const add = (type: 'machine' | 'process' | 'filament', name: string, dirty: Config) => {
    Object.assign(out, resolvePreset(s, type, name).user?.overrides ?? {}, dirty);
  };
  add('machine', s.printer, s.dirty.machine);
  add('process', s.process, s.dirty.process);
  add('filament', s.filamentSlots[0] ?? '', s.dirty.filament);
  if (s.bedType) out.curr_bed_type = s.bedType;
  if (s.filamentSlots.length > 1) {
    const { matrix, multiplier } = currentFlush(s);
    out.filament_colour = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
    out.flush_volumes_matrix = matrix;
    out.flush_multiplier = [multiplier];
  }
  for (const k of Object.keys(out)) out[k] = jobValue(k, out[k]);
  return out;
}

export function jobRequest(): JobRequest {
  const s = getState();
  const sys = (type: 'machine' | 'process' | 'filament', n: string) => resolvePreset(s, type, n).base?.name ?? n;
  const { plate, files } = s.project ? { plate: undefined, files: [{ name: s.project.name, blob: s.project as Blob }] } : plateJson(s.objects);
  return {
    files,
    vendor: printerPreset(s)?.vendor ?? 'BBL',
    printer: sys('machine', s.printer),
    process: sys('process', s.process),
    filaments: s.filamentSlots.map((f) => sys('filament', f)),
    overrides: overrides(),
    plate,
    events: s.events.map(eventArg),
    output: outputKind(),
  };
}

(window as unknown as { __jobId: unknown }).__jobId = () => getState().job.id;
(window as unknown as { __job: unknown }).__job = () => {
  const r = jobRequest();
  return {
    'files[]': r.files.map((f) => `${f.name} (${f.blob.size} bytes)`),
    vendor: r.vendor, printer: r.printer, process: r.process, 'filament[]': r.filaments,
    overrides: JSON.stringify(r.overrides), plate: r.plate && JSON.stringify(r.plate),
    events: r.events.length ? JSON.stringify(r.events) : undefined, output: r.output,
  };
};

let unsubscribe: (() => void) | null = null;

export async function slice() {
  const s = getState();
  if (!s.api || !s.objects.length || s.job.state === 'running' || s.job.state === 'submitting') return;
  setState({ job: { state: 'submitting', pct: 0, stage: 'uploading' } });
  try {
    const id = await s.api.submit(jobRequest());
    setState({ job: { id, state: 'running', pct: 0, stage: 'queued' } });
    unsubscribe?.();
    unsubscribe = s.api.events(id, {
      progress: (e) => setState((st) => ({ job: { ...st.job, pct: Math.max(st.job.pct, e.pct), stage: e.stage } })),
      done: (status) => setState({ job: { id, state: 'done', pct: 100, stage: 'done', status } }),
      error: (msg) => setState((st) => ({ job: { ...st.job, state: 'failed', error: msg } })),
    });
  } catch (e) {
    setState({ job: { state: 'failed', pct: 0, stage: '', error: String(e) } });
  }
}

export function cancelSlice() {
  unsubscribe?.();
  unsubscribe = null;
  setState({ job: { state: 'idle', pct: 0, stage: '' } });
}

export function download() {
  const s = getState();
  if (!s.api || s.job.state !== 'done' || !s.job.id) return;
  const a = document.createElement('a');
  a.href = s.api.downloadUrl(s.job.id);
  const base = (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf)$/i, '') ?? 'plate';
  a.download = s.job.status?.filename ?? `${base}.${outputKind()}`;
  document.body.appendChild(a);
  a.click();
  a.remove();
}
