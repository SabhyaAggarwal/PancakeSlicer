import { Box3, Matrix4, Vector3 } from 'three';
import { arrangeSettings } from './arrangeSettings';
import { firstLayer, overrides, plateSignature } from '../slice-input';
import { jobValue } from '../options';
import { mutation, responseError } from '../api/access';
import { getState, newId, printerPreset, resolvePreset, setState, toast, type PlateObject } from '../state/store';
import { undoable } from '../state/history';
import { bakeStl, instanceBox, instanceMatrix, loadMesh, objectMatrix, plateJson, request } from './geometry';
import { bedArea } from '../actions';
import { withRotation } from './rotation';

let busy = false;

export async function orientJob(o: PlateObject, operation: 'orient' | 'orient-scores'): Promise<Record<string, unknown> | undefined> {
  const s = getState();
  if (!s.api || s.api.mock) throw new Error('Auto orient requires the native slicing service.');
  const fd = new FormData();
  fd.append('operation', operation);
  fd.append('files[]', await bakeStl(o), 'orient.stl');
  fd.append('vendor', printerPreset(s)?.vendor ?? 'BBL');
  fd.append('printer', resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer);
  fd.append('process', resolvePreset(s, 'process', s.process).base?.name ?? s.process);
  for (const f of s.filamentSlots) fd.append('filament[]', resolvePreset(s, 'filament', f).base?.name ?? f);
  fd.append('overrides', JSON.stringify({ ...overrides(s),
    ...Object.fromEntries(Object.entries(o.settings).map(([key, value]) => [key, jobValue(key, value)])),
  }));
  const response = await mutation('/api/jobs', { method: 'POST', body: fd });
  if (!response.ok) throw await responseError(response);
  const { id } = await response.json() as { id: string };
  try {
    return await new Promise<Record<string, unknown> | undefined>((resolve, reject) => {
      const stop = s.api!.events(id, {
        progress() {},
        done(status) { stop(); resolve(status.record); },
        error(message) { stop(); reject(new Error(message)); },
      });
    });
  } finally {
    await s.api.cancel(id);
  }
}

async function orient(o: PlateObject): Promise<PlateObject> {
  const rotation = (await orientJob(o, 'orient'))?.rotation as number[];
  if (!Array.isArray(rotation) || rotation.length !== 9 || !rotation.every(Number.isFinite))
    throw new Error('The native tool did not return an orientation rotation.');
  return withRotation(o, rotation);
}

async function splitObject(o: PlateObject): Promise<PlateObject[]> {
  const file = await bakeStl(o);
  const pieces = await new Promise<Blob[]>((resolve, reject) => request({ type: 'split', file }, (m) => {
    if (m.type === 'pieces') resolve(m.pieces);
    else if (m.type === 'error') reject(new Error(m.message));
  }));
  if (pieces.length < 2) throw new Error("The selected object couldn't be split.");
  const result: PlateObject[] = [];
  for (let k = 0; k < pieces.length; k++) {
    const name = `${o.name.replace(/\.(stl|obj)$/i, '')}_${k + 1}`;
    const source = new File([pieces[k]], `${name}.stl`, { type: 'model/stl' });
    const geometry = await loadMesh(source);
    const centre = new Vector3(...geometry.userData.sourceCentre as [number, number, number]);
    const instances = o.instances.map((i) => {
      const m = instanceMatrix(o, i).multiply(objectMatrix(o).invert()).multiply(new Matrix4().makeTranslation(...centre.toArray()));
      const box = new Box3();
      const p = geometry.attributes.position;
      const point = new Vector3();
      for (let v = 0; v < p.count; v++) box.expandByPoint(point.fromBufferAttribute(p, v).applyMatrix4(m));
      const c = box.getCenter(new Vector3());
      return { ...i, id: newId(), x: c.x, y: c.y, z: 0 };
    });
    result.push({ ...o, id: newId(), name, source, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1], instances, settings: { ...o.settings } });
  }
  return result;
}

export async function run(operation: 'orient' | 'split', all: boolean) {
  const s = getState();
  if (busy || s.project || s.tab !== 'prepare') return;
  const objects = operation === 'orient' && (all || !s.selected)
    ? s.objects : s.objects.filter((o) => o.id === s.selected?.object);
  if (!objects.length) return;
  busy = true;
  const signature = plateSignature(s);
  const created: PlateObject[] = [];
  try {
    setState({ toast: operation === 'orient' ? 'Orienting objects…' : 'Splitting object…' });
    const replacements = new Map<number, PlateObject[]>();
    for (const o of objects) {
      const next = operation === 'orient' ? [await orient(o)] : await splitObject(o);
      replacements.set(o.id, next);
      if (operation === 'split') created.push(...next);
    }
    if (signature !== plateSignature(getState())) throw new Error('The plate changed during the operation. Try again.');
    const next = s.objects.flatMap((o) => replacements.get(o.id) ?? [o]);
    const first = replacements.get(objects[0].id)![0];
    undoable(operation === 'orient' ? 'Orient' : 'Split to Objects',
      () => setState({ objects: next, selected: { object: first.id, instance: first.instances[0].id }, toast: null }));
  } catch (error) {
    created.forEach((o) => o.geometry.dispose());
    toast(error instanceof Error ? error.message : String(error));
  } finally {
    busy = false;
  }
}

interface ArrangeResult { schema: string; plates: number; unplaced: number; objects: { instances: { plate: number | null; x?: number; y?: number; rot?: number }[] }[] }

export async function arrangeNative() {
  const s = getState();
  if (busy || !s.api || s.api.mock || s.project || !s.objects.length) return;
  busy = true;
  setState({ loading: 'Arranging' });
  try {
    const { plate, files } = await plateJson(s.objects, firstLayer(s));
    const fd = new FormData();
    fd.append('operation', 'arrange');
    for (const f of files) fd.append('files[]', f.blob, f.name);
    fd.append('plate', JSON.stringify(plate));
    fd.append('vendor', printerPreset(s)?.vendor ?? 'BBL');
    fd.append('printer', resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer);
    fd.append('process', resolvePreset(s, 'process', s.process).base?.name ?? s.process);
    for (const f of s.filamentSlots) fd.append('filament[]', resolvePreset(s, 'filament', f).base?.name ?? f);
    fd.append('overrides', JSON.stringify(overrides(s)));
    const { distance, rotation } = arrangeSettings();
    if (distance > 0) fd.append('arrange_spacing', String(distance));
    fd.append('arrange_rotation', rotation ? '1' : '0');
    const response = await mutation('/api/jobs', { method: 'POST', body: fd });
    if (!response.ok) throw await responseError(response);
    const { id } = await response.json() as { id: string };
    let result: ArrangeResult;
    try {
      result = await new Promise<ArrangeResult>((resolve, reject) => {
        const stop = s.api!.events(id, {
          progress() {},
          done(status) { stop(); resolve(status.record as unknown as ArrangeResult); },
          error(message) { stop(); reject(new Error(message)); },
        });
      });
    } finally {
      await s.api.cancel(id);
    }
    if (result?.schema !== 'orca.arrange.v1' || result.objects?.length !== s.objects.length) throw new Error('The native tool did not return an arrangement.');
    if (getState().objects !== s.objects) return;
    const area = bedArea(), xs = area.map((p) => p.x), width = Math.max(...xs) - Math.min(...xs);
    let away = 0;
    const objects = s.objects.map((o, k) => ({ ...o, instances: o.instances.map((inst, m) => {
      const r = result.objects[k].instances[m];
      if (!r || r.plate === null || r.x === undefined || r.y === undefined) { away++; return inst; }
      if (r.plate > 1) away++;
      const turned = { ...inst, rotZ: r.rot ?? inst.rotZ };
      const b = instanceBox(o, turned), cx = (b.min.x + b.max.x) / 2, cy = (b.min.y + b.max.y) / 2;
      return { ...turned, x: turned.x + r.x - cx + (r.plate - 1) * width * 1.2, y: turned.y + r.y - cy };
    }) }));
    undoable('Arrange', () => setState({ objects }));
    if (away) toast(`${away} instance${away > 1 ? 's do' : ' does'} not fit on the plate`);
  } catch (e) {
    toast(`Arrange failed: ${e instanceof Error ? e.message : e}`);
  } finally {
    busy = false;
    if (getState().loading === 'Arranging') setState({ loading: null });
  }
}
