import * as THREE from 'three';
import { unzip, strFromU8 } from 'fflate';
import type { Config } from '../api/types';
import { mutation, responseError } from '../api/access';
import { getState, newId, printerPreset, resolvePreset, setState, type Instance, type LayerEvent, type LayerRange, type NativeObject, type PlateObject, type SeamMode, type Volume, type VolumeType } from '../state/store';
import type { PlateJson } from '../api/types';
import { instanceBox, instanceMatrix, setNativePlate } from './geometry';
import { archivePainting, paintJson, type PaintKind } from './paint';

type Json = Record<string, unknown>;
type Rows = number[][];

const pristine = new WeakSet<THREE.BufferGeometry>();
export const markPristine = (g: THREE.BufferGeometry) => { pristine.add(g); return g; };

const fromRows = (r: Rows) => new THREE.Matrix4().set(...(r.flat() as [number, number, number, number, number, number, number, number, number, number, number, number, number, number, number, number]));
function rows(m: THREE.Matrix4): Rows {
  const e = m.elements;
  return [[e[0], e[4], e[8], e[12]], [e[1], e[5], e[9], e[13]], [e[2], e[6], e[10], e[14]], [e[3], e[7], e[11], e[15]]];
}

export async function volumeMesh(blob: Blob): Promise<THREE.BufferGeometry> {
  const zip = await new Promise<Record<string, Uint8Array>>(async (resolve, reject) =>
    unzip(new Uint8Array(await blob.arrayBuffer()), (err, data) => (err ? reject(err) : resolve(data))));
  const model = Object.entries(zip).find(([k]) => /\.model$/i.test(k))?.[1];
  if (!model) throw new Error('no 3D model in an exploded volume');
  const text = strFromU8(model), pos: number[] = [], idx: number[] = [];
  for (const m of text.matchAll(/<vertex\s+x="([^"]+)"\s+y="([^"]+)"\s+z="([^"]+)"/g)) pos.push(+m[1], +m[2], +m[3]);
  for (const m of text.matchAll(/<triangle\s+v1="(\d+)"\s+v2="(\d+)"\s+v3="(\d+)"/g)) idx.push(+m[1], +m[2], +m[3]);
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pos), 3));
  g.setIndex(new THREE.BufferAttribute(new Uint32Array(idx), 1));
  return g;
}

export function rawArrays(position: Float32Array, index: Uint32Array | Uint16Array | null): THREE.BufferGeometry {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(position, 3));
  if (index) g.setIndex(new THREE.BufferAttribute(index, 1));
  g.computeVertexNormals();
  g.computeBoundingBox();
  g.computeBoundingSphere();
  return g;
}

function rawGeometry(mesh: THREE.BufferGeometry, frame: THREE.Matrix4, transform: Rows): THREE.BufferGeometry {
  const g = mesh.clone().applyMatrix4(frame.clone().multiply(fromRows(transform)));
  g.computeVertexNormals();
  g.computeBoundingBox();
  g.computeBoundingSphere();
  return markPristine(g);
}

function worldBox(mesh: THREE.BufferGeometry, inst: Json, transform: Rows): THREE.Box3 {
  return new THREE.Box3().setFromBufferAttribute(mesh.clone().applyMatrix4(fromRows(inst.transform as Rows).multiply(fromRows(transform))).attributes.position as THREE.BufferAttribute);
}

const EVENT: Record<string, LayerEvent['type']> = { PausePrint: 'pause', Custom: 'custom', Template: 'template', ColorChange: 'colour', ToolChange: 'change' };
function plateEvents(plate: Json): LayerEvent[] {
  const list = ((plate.custom_gcode as Json | undefined)?.gcodes ?? []) as Json[];
  return list.flatMap((g) => {
    const type = EVENT[String(g.type)];
    if (!type) return [];
    const text = type === 'custom' ? String(g.extra ?? '') : type === 'change' ? String(g.extruder ?? 1) : undefined;
    return [{ z: Number(g.print_z), type, ...(text !== undefined ? { text } : {}) }];
  });
}

function plateObject(entry: Json, group: Json[], meshes: THREE.BufferGeometry[], files: Record<string, Blob>): PlateObject {
  const volumes = entry.volumes as Json[];
  const main = Math.max(0, volumes.findIndex((v) => v.type === 'part'));
  const first = group[0];
  const linear = fromRows(first.transform as Rows).setPosition(0, 0, 0);
  const m = new THREE.Matrix4().makeRotationZ((-Number(first.rot ?? 0) * Math.PI) / 180).multiply(linear);
  const mirrored = m.determinant() < 0;
  const frame = mirrored ? new THREE.Matrix4().makeScale(-1, 1, 1).multiply(m) : m;
  const base = { rotX: 0, rotY: 0, scale: [mirrored ? -1 : 1, 1, 1] as [number, number, number] };
  const instances: Instance[] = group.map((inst) => {
    const box = worldBox(meshes[main], inst, volumes[main].transform as Rows);
    const x = (box.min.x + box.max.x) / 2, y = (box.min.y + box.max.y) / 2, z = box.min.z, rotZ = Number(inst.rot ?? 0);
    return { id: newId(), x, y, z, rotZ, printable: inst.printable !== false, native: { json: inst, x, y, z, rotZ } };
  });
  const parts: Volume[] = volumes.flatMap((v, k) => k === main ? [] : [{
    id: newId(), name: String(v.name ?? `volume ${k + 1}`), ...volumeType(v),
    geometry: rawGeometry(meshes[k], frame, v.transform as Rows), matrix: new THREE.Matrix4().elements,
    extruder: Number(v.extruder ?? 0), settings: (v.settings ?? {}) as Config,
    ...(v.paint ? { paint: archivePainting(v.paint as Record<PaintKind, Record<string, string>>) ?? undefined } : {}), native: k,
  }]);
  const ranges: LayerRange[] = ((entry.ranges ?? []) as Json[]).map((r) => ({ id: newId(), z: r.z as [number, number], layer_height: Number(r.layer_height ?? 0.2), settings: (r.settings ?? {}) as Config }));
  const layerProfile = entry.layer_height_profile as number[] | undefined;
  const mainFile = String(volumes[main].file);
  const native: NativeObject = { entry: { ...entry, instances: undefined }, frame: frame.elements, base, main, files, ...(layerProfile ? { layerProfile } : {}) };
  return {
    id: newId(), name: String(entry.name ?? 'object'), geometry: rawGeometry(meshes[main], frame, volumes[main].transform as Rows), ...base,
    extruder: Number(entry.extruder ?? 1), instances, source: files[mainFile] as File, settings: (entry.settings ?? {}) as Config,
    ...(parts.length ? { volumes: parts } : {}), ...(ranges.length ? { ranges } : {}), ...(layerProfile ? { layerProfile } : {}),
    ...(volumes[main].paint ? { paint: archivePainting(volumes[main].paint as Record<PaintKind, Record<string, string>>) ?? undefined } : {}), native,
  };
}

export async function nativeGeometry(n: NativeObject): Promise<THREE.BufferGeometry> {
  const v = (n.entry.volumes as Json[])[n.main];
  return rawGeometry(await volumeMesh(n.files[String(v.file)]), new THREE.Matrix4().fromArray(n.frame), v.transform as Rows);
}

function volumeType(v: Json): { type: VolumeType; seamMode?: SeamMode } {
  const t = String(v.type);
  const m = /^precise_seam_(center|left|right|enforced|blocked|neutral)$/.exec(t);
  if (m) return { type: 'seam', seamMode: m[1] as SeamMode };
  return t === 'seam' ? { type: 'seam', seamMode: (String(v.seam_mode ?? 'center') as SeamMode) } : { type: t as VolumeType };
}

export async function explodeProject(file: File): Promise<{ settings: Config; plates: { index: number; name: string; settings: Config; objects: PlateObject[]; events: LayerEvent[] }[] }> {
  const s = getState();
  const fd = new FormData();
  fd.append('operation', 'explode');
  fd.append('files[]', file, file.name);
  fd.append('vendor', printerPreset(s)?.vendor ?? 'BBL');
  fd.append('printer', resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer);
  fd.append('process', resolvePreset(s, 'process', s.process).base?.name ?? s.process);
  for (const f of s.filamentSlots) fd.append('filament[]', resolvePreset(s, 'filament', f).base?.name ?? f);
  const response = await mutation('/api/jobs', { method: 'POST', body: fd });
  if (!response.ok) throw await responseError(response);
  const { id } = await response.json() as { id: string };
  try {
    const record = await new Promise<Json>((resolve, reject) => {
      const stop = s.api!.events(id, { progress() {}, done(st) { stop(); resolve((st.record ?? {}) as Json); }, error(m) { stop(); reject(new Error(m)); } });
    });
    const objects = (record.objects ?? []) as Json[];
    const files: Record<string, Blob> = {};
    await Promise.all(objects.flatMap((o) => (o.volumes as Json[]).map(async (v) => {
      const r = await fetch(`/api/jobs/${id}/exploded/${encodeURIComponent(String(v.file))}`);
      if (!r.ok) throw await responseError(r);
      files[String(v.file)] = new File([await r.blob()], String(v.file));
    })));
    const meshes = new Map<string, THREE.BufferGeometry>();
    await Promise.all(Object.entries(files).map(async ([name, blob]) => meshes.set(name, await volumeMesh(blob))));
    const plates = ((record.plates ?? []) as Json[]).map((p) => ({ index: Number(p.index), name: String(p.name ?? ''), settings: (p.settings ?? {}) as Config, objects: [] as PlateObject[], events: plateEvents(p) }));
    if (!plates.length) plates.push({ index: 1, name: '', settings: {}, objects: [], events: [] });
    for (const o of objects) {
      const vols = o.volumes as Json[];
      const own = Object.fromEntries(vols.map((v) => [String(v.file), files[String(v.file)]]));
      const byPlate = new Map<number, Json[]>();
      for (const inst of (o.instances ?? []) as Json[]) {
        const p = Number(inst.plate ?? 1);
        byPlate.set(p, [...(byPlate.get(p) ?? []), inst]);
      }
      for (const [p, group] of byPlate) {
        const plate = plates.find((x) => x.index === p) ?? plates[0];
        plate.objects.push(plateObject(o, group, vols.map((v) => meshes.get(String(v.file))!), own));
      }
    }
    return { settings: (record.settings ?? {}) as Config, plates };
  } finally {
    await s.api!.cancel(id);
  }
}

const KINDS: PaintKind[] = ['support', 'seam', 'fuzzy', 'color'];
function nativePaint(target: { geometry: THREE.BufferGeometry; paint?: PlateObject['paint'] }, original: unknown) {
  const now = paintJson(target) ?? {};
  for (const k of KINDS) if ((original as Json | undefined)?.[k] && !now[k]) now[k] = {};
  return Object.keys(now).length ? now : undefined;
}

const sameFrame = (o: PlateObject, n: NativeObject) => o.rotX === n.base.rotX && o.rotY === n.base.rotY && o.scale.every((v, k) => v === n.base.scale[k]);

function nativeEntry(o: PlateObject, settings: (c: Config) => Record<string, string>, profile: () => number[] | undefined, prefix: string): { entry: PlateJson['objects'][number]; files: { name: string; blob: () => Blob | Promise<Blob> }[] } | null {
  const n = o.native;
  if (!n || !pristine.has(o.geometry)) return null;
  const frame = new THREE.Matrix4().fromArray(n.frame), inverse = frame.clone().invert();
  const original = n.entry.volumes as Json[];
  const files: { name: string; blob: () => Blob | Promise<Blob> }[] = [];
  const volume = (k: number, patch: Json): Json => {
    const v = original[k];
    files.push({ name: String(v.file), blob: () => n.files[String(v.file)] });
    return { ...v, ...patch };
  };
  const out: Json[] = [];
  const web = new Map((o.volumes ?? []).filter((v) => v.native !== undefined && pristine.has(v.geometry)).map((v) => [v.native!, v]));
  original.forEach((v, k) => {
    if (k === n.main) return void out.push(volume(k, { paint: nativePaint(o, v.paint) }));
    const w = web.get(k);
    if (!w) return;
    const moved = !new THREE.Matrix4().fromArray(w.matrix).equals(new THREE.Matrix4());
    out.push(volume(k, {
      name: w.name, type: w.type, ...(w.type === 'seam' ? { seam_mode: w.seamMode ?? 'center' } : {}), settings: settings(w.settings), paint: w.type === 'part' ? nativePaint(w, v.paint) : undefined,
      extruder: w.extruder || undefined,
      ...(moved ? { transform: rows(inverse.clone().multiply(new THREE.Matrix4().fromArray(w.matrix)).multiply(frame).multiply(fromRows(v.transform as Rows))) } : {}),
    }));
  });
  (o.volumes ?? []).filter((v) => !(v.native !== undefined && pristine.has(v.geometry))).forEach((v, k) => {
    const name = `${prefix}.added${k + 1}.stl`;
    files.push({ name, blob: () => rawStl(v.geometry) });
    out.push({ file: name, name: v.name, type: v.type, ...(v.type === 'seam' ? { seam_mode: v.seamMode ?? 'center' } : {}), mesh_space: 'volume', transform: rows(inverse.clone().multiply(new THREE.Matrix4().fromArray(v.matrix))),
      ...(Object.keys(v.settings).length ? { settings: settings(v.settings) } : {}), ...(v.extruder ? { extruder: v.extruder } : {}),
      ...(v.type === 'part' && paintJson(v) ? { paint: paintJson(v) } : {}) });
  });
  const keep = sameFrame(o, n);
  const instances = o.instances.map((i): Json => {
    const nat = i.native;
    if (nat && keep) {
      const { plate: _, ...json } = nat.json;
      if (i.x === nat.x && i.y === nat.y && (i.z ?? 0) === nat.z && i.rotZ === nat.rotZ) return { ...json, printable: i.printable !== false };
      const b = instanceBox(o, i);
      return { ...json, x: (b.min.x + b.max.x) / 2, y: (b.min.y + b.max.y) / 2, z: b.min.z, rot: i.rotZ, printable: i.printable !== false };
    }
    return { transform: rows(instanceMatrix(o, i).multiply(frame)), printable: i.printable !== false, ...(nat ? { identify_id: nat.json.identify_id } : {}) };
  });
  const { volumes: _v, instances: _i, layer_height_profile: lhp, ranges: _r, settings: _s, extruder: _e, name: _n, ...rest } = n.entry;
  const lh = o.layerProfile === n.layerProfile ? lhp : profile();
  return {
    entry: {
      ...rest, name: o.name, extruder: o.extruder, settings: settings(o.settings), volumes: out, instances,
      ...(o.ranges?.length ? { ranges: o.ranges.map((r) => ({ z: r.z, layer_height: r.layer_height, settings: settings(r.settings) })) } : {}),
      ...(lh ? { layer_height_profile: lh } : {}),
    } as unknown as PlateJson['objects'][number],
    files,
  };
}

function rawStl(g: THREE.BufferGeometry): Blob {
  const p = g.attributes.position.array as Float32Array, idx = g.index?.array;
  const n = idx ? idx.length / 3 : p.length / 9;
  const buf = new ArrayBuffer(84 + 50 * n), dv = new DataView(buf);
  dv.setUint32(80, n, true);
  for (let t = 0; t < n; t++)
    for (let k = 0; k < 3; k++) {
      const v = idx ? idx[3 * t + k] : 3 * t + k;
      for (let a = 0; a < 3; a++) dv.setFloat32(84 + 50 * t + 12 + 12 * k + 4 * a, p[3 * v + a], true);
    }
  return new Blob([buf]);
}

setNativePlate(nativeEntry);

function towers(config: Config, plates: number[]): Record<number, [number, number]> {
  const list = (v: unknown) => (Array.isArray(v) ? v : String(v ?? '').split(',')).map(Number);
  const xs = list(config.wipe_tower_x), ys = list(config.wipe_tower_y);
  return Object.fromEntries(plates.flatMap((p, k) => (Number.isFinite(xs[k]) && Number.isFinite(ys[k]) ? [[p, [xs[k], ys[k]]]] : [])));
}

export async function openExplodedProject(file: File, adopt: (config: Config, slots: number, plateKeys: Set<string>) => void) {
  const { readProject } = await import('./threemf');
  const { items, materials } = await readProject(file);
  const { settings, plates } = await explodeProject(file);
  if (!plates.some((p) => p.objects.length)) throw new Error('the project has no printable objects');
  const slots = Math.max(1, materials.colours.length, ...items.map((i) => i.extruder));
  adopt(materials.config, slots, new Set(materials.plates.flatMap((p) => Object.keys(p.settings))));
  const first = plates[0];
  setState((st) => ({
    project: null, projectName: file.name, projectMaterials: null, objects: first.objects, events: first.events, plateIndex: first.index,
    projectPlates: plates.map((p) => ({ index: p.index, name: p.name, origin: [0, 0] as [number, number], settings: p.settings, objects: p.objects, events: p.events })),
    filamentSlots: Array.from({ length: slots }, (_, i) => st.filamentSlots[i] ?? st.filamentSlots[0] ?? ''),
    slotColours: materials.colours.map((c) => c || undefined), flush: { matrix: materials.matrix, multiplier: materials.multiplier },
    selected: null, processScope: 'global',
    wipeTowerPos: towers(settings.wipe_tower_x !== undefined ? settings : materials.config, plates.map((p) => p.index)),
  }));
}
