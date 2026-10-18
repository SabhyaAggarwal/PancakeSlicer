import type { Config, ConfigValue, JobRequest, PresetCatalog } from './api/types';
import { sliceProgress } from './api/progress';
import { connect } from './api/client';
import { currentFlush } from './components/FlushDialog';
import { eventArg } from './components/LayerEvents';
import { jobValue } from './options';
import { shortTime } from './preview/format';
import { filamentColor } from './plater/Plater3D';
import { arrange, loadMesh, loadProject, nextFreeSpot, parsePoints, placeholderGeometry, plateJson } from './plater/geometry';
import type { ProjectItem } from './plater/threemf';
import {
  DEFAULT_PRINTER, DEMO_MODELS, effectiveConfig, getState, markStale, newId, printerPreset, resolvePreset, selectPrinter, setState, toast, type PlateObject,
} from './state/store';

const VENDORS = ['BBL', 'Prusa'];

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
  const saved = localStorage.getItem('orca.printer');
  const first = localStorage.getItem('orca.vendor') ?? 'BBL';
  const vendors = [first, ...VENDORS.filter((v) => v !== first)];
  try {
    const [cat, userPresets] = await Promise.all([
      api.presets(first, DEMO_MODELS, (c) => mergeCatalog(first, c)),
      api.userPresets().catch(() => []),
    ]);
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
      const geometry = await loadMesh(f, (pct) => setState({ loading: `Loading ${f.name} ${Math.round(pct)}%` }));
      if (getState().loading?.startsWith('Loading ')) setState({ loading: null });
      added.push({
        id: newId(), name: f.name, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1], extruder: 1, source: f, settings: {},
        instances: [{ id: newId(), x: 0, y: 0, rotZ: 0 }],
      });
    } catch (e) {
      if (getState().loading?.startsWith('Loading ')) setState({ loading: null });
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
  let objects: PlateObject[] = [];
  const patch = (i: number, f: (o: PlateObject) => PlateObject) => {
    const id = objects[i]?.id;
    if (getState().project === file) setState((st) => ({ objects: st.objects.map((o) => (o.id === id ? f(o) : o)) }));
  };
  try {
    await loadProject(file, {
      items(items: ProjectItem[]) {
        if (!items.length) return toast(`${file.name}: the project has no printable objects`);
        const slots = Math.max(s.filamentSlots.length, ...items.map((i) => i.extruder));
        objects = items.map((it) => ({
          id: newId(), name: it.name, geometry: placeholderGeometry(), rotX: 0, rotY: 0, scale: [1, 1, 1],
          extruder: it.extruder, source: file, settings: {}, project: { file: file.name, placeholder: true },
          instances: [{ id: newId(), x: it.origin[0], y: it.origin[1], rotZ: 0 }],
        }));
        setState((st) => ({
          objects, project: file, selected: null, processScope: 'global',
          filamentSlots: st.filamentSlots.length >= slots ? st.filamentSlots : [...st.filamentSlots, ...Array(slots - st.filamentSlots.length).fill(st.filamentSlots[0] ?? '')],
          job: markStale(st.job),
        }));
      },
      mesh: (i, geometry, centre) =>
        patch(i, (o) => ({ ...o, geometry, project: { file: file.name, placeholder: false }, instances: [{ ...o.instances[0], x: centre.x, y: centre.y }] })),
      missing: (i) => patch(i, (o) => ({ ...o, project: { file: file.name, placeholder: true, missing: true } })),
    });
  } catch (e) {
    toast(`${file.name}: ${e instanceof Error ? e.message : e}`);
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

export async function jobRequest(): Promise<JobRequest> {
  const s = getState();
  const sys = (type: 'machine' | 'process' | 'filament', n: string) => resolvePreset(s, type, n).base?.name ?? n;
  const { plate, files } = s.project ? { plate: undefined, files: [{ name: s.project.name, blob: s.project as Blob }] } : await plateJson(s.objects);
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
(window as unknown as { __job: unknown }).__job = async () => {
  const r = await jobRequest();
  return {
    'files[]': r.files.map((f) => `${f.name} (${f.blob.size} bytes)`),
    vendor: r.vendor, printer: r.printer, process: r.process, 'filament[]': r.filaments,
    overrides: JSON.stringify(r.overrides), plate: r.plate && JSON.stringify(r.plate),
    events: r.events.length ? JSON.stringify(r.events) : undefined, output: r.output,
  };
};

let unsubscribe: (() => void) | null = null;
let sliceGeneration = 0;

export async function slice() {
  const s = getState();
  if (!s.api || !s.objects.length || s.job.state === 'running' || s.job.state === 'submitting') return;
  const generation = ++sliceGeneration;
  setState({ job: { state: 'submitting', pct: 0, stage: 'uploading' } });
  try {
    const request = await jobRequest();
    if (generation !== sliceGeneration) return;
    const id = await s.api.submit(request);
    if (generation !== sliceGeneration) {
      await s.api.cancel(id);
      return;
    }
    setState({ job: { id, state: 'running', pct: 0, stage: 'queued' } });
    unsubscribe?.();
    const warnings: string[] = [];
    unsubscribe = s.api.events(id, {
      progress: (e) => {
        if (generation !== sliceGeneration) return;
        const progress = sliceProgress(e);
        if (e.stage === 'verify' && e.pct >= 100 && request.output === 'gcode.3mf') progress.stage = 'Packaging sliced plate';
        setState((st) => progress.pct < st.job.pct ? {} : { job: { ...st.job, ...progress } });
      },
      done: (status) => generation === sliceGeneration && setState({ job: { id, state: 'done', pct: 100, stage: 'done', status, warnings } }),
      error: (msg) => generation === sliceGeneration && setState((st) => ({ job: { ...st.job, state: 'failed', error: msg } })),
      log: (stream, line) => {
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
  const s = getState();
  unsubscribe?.();
  unsubscribe = null;
  if (s.job.id) void s.api?.cancel(s.job.id).catch(() => undefined);
  setState({ job: { state: 'canceled', pct: 0, stage: '' } });
}

export function exportFilename(): string {
  const s = getState();
  const base = (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf)$/i, '') ?? 'plate';
  const time = Number((s.job.status?.record as { text_summary?: Record<string, string> } | undefined)?.text_summary?.estimated_time_s);
  const firstTool = Math.max(0, Math.min(...s.objects.map((o) => o.extruder - 1)));
  const vars: Config = {
    ...effectiveConfig(s, 'machine'), ...effectiveConfig(s, 'filament'), ...effectiveConfig(s, 'process'), ...overrides(),
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
  return `${stem}.${outputKind()}`;
}

export async function download() {
  const s = getState();
  if (!s.api || s.job.state !== 'done' || !s.job.id) return;
  let href: string;
  try {
    const r = await fetch(s.api.downloadUrl(s.job.id));
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    href = URL.createObjectURL(await r.blob());
  } catch (e) {
    toast(`Export failed: ${e instanceof Error ? e.message : e}`);
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
