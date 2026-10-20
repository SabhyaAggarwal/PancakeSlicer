import { Box3, Euler, Matrix4, Vector3 } from 'three';
import { overrides, plateSignature } from '../slice-input';
import { jobValue } from '../options';
import { mutation, responseError } from '../api/access';
import { getState, newId, printerPreset, resolvePreset, setState, toast, type PlateObject } from '../state/store';
import { undoable } from '../state/history';
import { bakeStl, instanceMatrix, loadMesh, objectMatrix, request } from './geometry';

const DEG = Math.PI / 180;
let busy = false;

async function orient(o: PlateObject): Promise<PlateObject> {
  const s = getState();
  if (!s.api || s.api.mock) throw new Error('Auto orient requires the native slicing service.');
  const fd = new FormData();
  fd.append('operation', 'orient');
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
    const rotation = await new Promise<number[]>((resolve, reject) => {
      const stop = s.api!.events(id, {
        progress() {},
        done(status) { stop(); resolve(status.record?.rotation as number[]); },
        error(message) { stop(); reject(new Error(message)); },
      });
    });
    if (!Array.isArray(rotation) || rotation.length !== 9 || !rotation.every(Number.isFinite))
      throw new Error('The native tool did not return an orientation rotation.');
    const r = new Matrix4().set(
      rotation[0], rotation[1], rotation[2], 0,
      rotation[3], rotation[4], rotation[5], 0,
      rotation[6], rotation[7], rotation[8], 0, 0, 0, 0, 1,
    );
    r.multiply(new Matrix4().makeRotationY(o.rotY * DEG)).multiply(new Matrix4().makeRotationX(o.rotX * DEG));
    const angles = new Euler().setFromRotationMatrix(r, 'ZYX');
    return { ...o, rotX: angles.x / DEG, rotY: angles.y / DEG,
      instances: o.instances.map((i) => ({ ...i, rotZ: i.rotZ + angles.z / DEG, z: 0 })) };
  } finally {
    await s.api.cancel(id);
  }
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
