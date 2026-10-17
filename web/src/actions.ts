import type { Config, PresetCatalog } from './api/types';
import { connect } from './api/client';
import { arrange, nextFreeSpot, parsePoints, loadMesh, plateJson } from './plater/geometry';
import {
  DEFAULT_PRINTER, DEMO_MODELS, getState, newId, printerPreset, resolvePreset, selectPrinter, setState, toast, type PlateObject,
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
  const list = [...files];
  if (!list.length) return;
  setState({ tab: 'prepare' });
  const added: PlateObject[] = [];
  for (const f of list) {
    try {
      const geometry = await loadMesh(f);
      added.push({
        id: newId(), name: f.name, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1], extruder: 1, source: f,
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

export function openFileDialog() {
  const input = document.createElement('input');
  input.type = 'file';
  input.multiple = true;
  input.accept = '.stl,.obj,.STL,.OBJ';
  input.onchange = () => input.files && void importFiles(input.files);
  input.click();
}

export function arrangeAll() {
  setState((s) => ({ objects: arrange(s.objects, bedArea()) }));
}

export function addInstance() {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o) return;
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
  return out;
}

let unsubscribe: (() => void) | null = null;

export async function slice() {
  const s = getState();
  if (!s.api || !s.objects.length || s.job.state === 'running' || s.job.state === 'submitting') return;
  const sys = (type: 'machine' | 'process' | 'filament', n: string) => resolvePreset(s, type, n).base?.name ?? n;
  const { plate, files } = plateJson(s.objects);
  setState({ job: { state: 'submitting', pct: 0, stage: 'uploading' } });
  try {
    const id = await s.api.submit({
      files,
      vendor: printerPreset(s)?.vendor ?? 'BBL',
      printer: sys('machine', s.printer),
      process: sys('process', s.process),
      filaments: s.filamentSlots.map((f) => sys('filament', f)),
      overrides: overrides(),
      plate,
      output: outputKind(),
    });
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
  const base = s.objects[0]?.name.replace(/\.(stl|obj)$/i, '') ?? 'plate';
  a.download = s.job.status?.filename ?? `${base}.${outputKind()}`;
  document.body.appendChild(a);
  a.click();
  a.remove();
}
