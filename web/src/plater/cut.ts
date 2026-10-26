import * as THREE from 'three';
import { bedArea } from '../actions';
import { mutation, responseError } from '../api/access';
import { plateSignature } from '../slice-input';
import { undoable } from '../state/history';
import { getState, newId, printerPreset, resolvePreset, setState, toast, type CutPlane, type Instance, type PlateObject, type State, type Volume } from '../state/store';
import { bakeStl, instanceBox, instanceMatrix, loadMesh, nearestFreeSpot, objectMatrix } from './geometry';
import { centred } from './objectOps';

const DEG = Math.PI / 180;

const cuttable = (s: State, o: PlateObject) => !o.volumes?.length && (!o.project || (!!s.features.edit && !o.project.placeholder));

export function cutTarget(s: State = getState()): { o: PlateObject; inst: Instance } | null {
  const sel = s.selected;
  const o = sel && !sel.all ? s.objects.find((ob) => ob.id === sel.object) : undefined;
  const inst = o?.instances.find((i) => i.id === sel!.instance);
  return o && inst && cuttable(s, o) ? { o, inst } : null;
}

export function cutBlocked(s: State = getState()): string | null {
  if (!s.features.cut) return 'Cut needs a slicing service whose tool has --cut-only';
  const o = s.objects.find((ob) => ob.id === s.selected?.object);
  if (!o || s.selected?.all) return 'Select one object to cut';
  if (o.project && !s.features.edit) return 'Objects of an opened 3MF project need a tool with --edit to be cut';
  if (o.project?.placeholder) return 'The project has no mesh for this object';
  if (o.volumes?.length) return 'Objects with parts or modifiers cannot be cut yet';
  return null;
}

export function cutNormal(c: Pick<CutPlane, 'rotX' | 'rotY'>): THREE.Vector3 {
  return new THREE.Vector3(0, 0, 1).applyEuler(new THREE.Euler(c.rotX * DEG, c.rotY * DEG, 0, 'YXZ'));
}

export function cutPoint(s: State, c: CutPlane): THREE.Vector3 | null {
  const t = cutTarget(s);
  if (!t) return null;
  const b = instanceBox(t.o, t.inst);
  return new THREE.Vector3((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, c.z);
}

export function initialCut(s: State = getState()): CutPlane | null {
  const t = cutTarget(s);
  if (!t) return null;
  const b = instanceBox(t.o, t.inst);
  return { z: (b.min.z + b.max.z) / 2, rotX: 0, rotY: 0, keepUpper: true, keepLower: true, placeLower: false };
}

export function toggleCut() {
  const s = getState();
  if (s.gizmo === 'cut') return setState({ gizmo: 'none', cut: null });
  if (cutBlocked(s)) return;
  setState({ gizmo: 'cut', cut: initialCut(s) });
}

export const setCut = (patch: Partial<CutPlane>) => setState((s) => (s.cut ? { cut: { ...s.cut, ...patch } } : {}));

export function canPerformCut(s: State = getState()): boolean {
  const t = cutTarget(s);
  const c = s.cut;
  if (!t || !c || !(c.keepUpper || c.keepLower)) return false;
  const b = instanceBox(t.o, t.inst);
  const n = cutNormal(c), p = cutPoint(s, c)!;
  let lo = Infinity, hi = -Infinity;
  for (let k = 0; k < 8; k++) {
    const d = new THREE.Vector3(k & 1 ? b.max.x : b.min.x, k & 2 ? b.max.y : b.min.y, k & 4 ? b.max.z : b.min.z).sub(p).dot(n);
    lo = Math.min(lo, d);
    hi = Math.max(hi, d);
  }
  return lo < -1e-3 && hi > 1e-3;
}

interface Piece { side: 'upper' | 'lower'; triangles: number; open_edges: number; volume_mm3: number }

let busy = false;

export async function performCut() {
  const s = getState();
  const t = cutTarget(s);
  const c = s.cut;
  if (busy || !t || !c || !s.api || s.api.mock || !canPerformCut(s)) return;
  const { o, inst } = t;
  busy = true;
  const signature = plateSignature(s);
  const created: PlateObject[] = [];
  try {
    setState({ toast: null, loading: 'Cutting object' });
    const toStl = instanceMatrix(o, inst).multiply(objectMatrix(o).invert()).invert();
    const p = cutPoint(s, c)!.applyMatrix4(toStl);
    const n = cutNormal(c).transformDirection(toStl);
    const fd = new FormData();
    fd.append('operation', 'cut');
    fd.append('cut', [p.x, p.y, p.z, n.x, n.y, n.z].map((v) => v.toFixed(6)).join(','));
    fd.append('cut_keep', c.toParts || c.keepUpper && c.keepLower ? 'both' : c.keepUpper ? 'upper' : 'lower');
    if (c.placeLower && !c.toParts) fd.append('cut_place_lower', '1');
    fd.append('files[]', await bakeStl(o), 'cut.stl');
    fd.append('vendor', printerPreset(s)?.vendor ?? 'BBL');
    fd.append('printer', resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer);
    fd.append('process', resolvePreset(s, 'process', s.process).base?.name ?? s.process);
    for (const f of s.filamentSlots) fd.append('filament[]', resolvePreset(s, 'filament', f).base?.name ?? f);
    const response = await mutation('/api/jobs', { method: 'POST', body: fd });
    if (!response.ok) throw await responseError(response);
    const { id } = await response.json() as { id: string };
    let files: { piece: Piece; file: File }[];
    try {
      const pieces = await new Promise<Piece[]>((resolve, reject) => {
        const stop = s.api!.events(id, {
          progress() {},
          done(status) { stop(); resolve((status.record?.pieces ?? []) as Piece[]); },
          error(message) { stop(); reject(new Error(message)); },
        });
      });
      if (!pieces.length) throw new Error('The native tool returned no cut pieces.');
      const base = o.name.replace(/\.(stl|obj)$/i, '');
      files = await Promise.all(pieces.map(async (piece) => {
        const r = await fetch(`${s.api!.downloadUrl(id)}?piece=${piece.side}`);
        if (!r.ok) throw await responseError(r);
        const name = pieces.length > 1 ? `${base}_${piece.side === 'upper' ? 'A' : 'B'}` : base;
        return { piece, file: new File([await r.blob()], `${name}.stl`, { type: 'model/stl' }) };
      }));
    } finally {
      await s.api.cancel(id);
    }
    const turned = (side: Piece['side']) => (side === 'upper' && (c.rotX !== 0 || c.rotY !== 0 || !!c.flipUpper)) || (side === 'lower' && c.placeLower);
    if (c.toParts) return void await cutToParts(o, files, signature);
    const others = s.objects.filter((ob) => ob.id !== o.id);
    const placed: PlateObject[] = [];
    for (const side of ['lower', 'upper'] as const) {
      const f = files.find((x) => x.piece.side === side);
      if (!f) continue;
      const geometry = await loadMesh(f.file);
      const centre = new THREE.Vector3(...geometry.userData.sourceCentre as [number, number, number]);
      const piece: PlateObject = { ...o, project: undefined, id: newId(), name: f.file.name.replace(/\.stl$/, ''), source: f.file, geometry, rotX: side === 'upper' && c.flipUpper ? 180 : 0, rotY: 0,
        scale: [1, 1, 1], settings: { ...o.settings }, instances: [] };
      for (const i of o.instances) {
        const m = instanceMatrix(o, i).multiply(objectMatrix(o).invert()).multiply(new THREE.Matrix4().makeTranslation(centre.x, centre.y, centre.z));
        const box = new THREE.Box3().setFromBufferAttribute(geometry.attributes.position as THREE.BufferAttribute).applyMatrix4(m);
        let at = box.getCenter(new THREE.Vector3());
        const moved: Instance = { ...i, id: newId(), x: at.x, y: at.y, z: 0 };
        if (turned(side) || (side === 'upper' && files.length > 1)) {
          const size = instanceBox({ ...piece, instances: [moved] }, moved).getSize(new THREE.Vector3());
          const spot = nearestFreeSpot([...others, ...placed, piece], new THREE.Vector2(size.x + 4, size.y + 4), new THREE.Vector2(at.x, at.y), bedArea());
          if (spot) at = new THREE.Vector3(spot.x, spot.y, 0);
          moved.x = at.x;
          moved.y = at.y;
        }
        piece.instances.push(moved);
      }
      created.push(piece);
      placed.push(piece);
    }
    if (signature !== plateSignature(getState())) throw new Error('The plate changed during the cut. Try again.');
    const pieces = created.slice().reverse();
    const objects = s.objects.flatMap((ob) => (ob.id === o.id ? pieces : [ob]));
    const open = files.filter((f) => f.piece.open_edges > 0);
    undoable('Cut by Plane', () => setState({ objects, selected: { object: pieces[0].id, instance: pieces[0].instances[0].id, allInstances: true },
      gizmo: 'none', cut: null, toast: open.length ? `${open.map((f) => f.file.name).join(', ')}: open edges after the cut` : null }));
  } catch (error) {
    created.forEach((x) => x.geometry.dispose());
    toast(error instanceof Error ? error.message : String(error));
  } finally {
    busy = false;
    if (getState().loading === 'Cutting object') setState({ loading: null });
  }
}

async function cutToParts(o: PlateObject, files: { piece: Piece; file: File }[], signature: string) {
  const s = getState(), inst = o.instances[0];
  const load = async (side: Piece['side']) => {
    const f = files.find((x) => x.piece.side === side);
    if (!f) throw new Error('The native tool returned one piece: the plane does not cross the object.');
    const geometry = await loadMesh(f.file);
    const toWorld = instanceMatrix(o, inst).multiply(objectMatrix(o).invert())
      .multiply(new THREE.Matrix4().makeTranslation(...geometry.userData.sourceCentre as [number, number, number]));
    return { f, geometry, toWorld };
  };
  const lower = await load('lower'), upper = await load('upper');
  const box = new THREE.Box3().setFromBufferAttribute(lower.geometry.attributes.position as THREE.BufferAttribute).applyMatrix4(lower.toWorld);
  const at = box.getCenter(new THREE.Vector3());
  const base: PlateObject = { ...o, project: undefined, id: newId(), name: o.name.replace(/\.(stl|obj)$/i, ''), source: lower.f.file, geometry: lower.geometry,
    rotX: 0, rotY: 0, scale: [1, 1, 1], settings: { ...o.settings }, volumes: undefined, paint: undefined, layerProfile: undefined,
    instances: [{ ...inst, id: newId(), x: at.x, y: at.y, z: 0 }] };
  const [geometry, back] = centred(upper.geometry);
  const part: Volume = { id: newId(), name: `${base.name}_A`, type: 'part', geometry, extruder: 0, settings: {},
    matrix: instanceMatrix(base, base.instances[0]).invert().multiply(upper.toWorld).multiply(back).elements };
  const object: PlateObject = { ...base, volumes: [part] };
  if (signature !== plateSignature(getState())) throw new Error('The plate changed during the cut. Try again.');
  undoable('Cut by Plane', () => setState({ objects: s.objects.flatMap((ob) => (ob.id === o.id ? [object] : [ob])),
    selected: { object: object.id, instance: object.instances[0].id, allInstances: true }, gizmo: 'none', cut: null, toast: null }));
}
