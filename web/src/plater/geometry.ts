import * as THREE from 'three';
import { mergeVertices } from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import type { ConfigValue, PlateJson } from '../api/types';
import { toolValue } from '../options';
import type { Instance, PlateObject, Volume } from '../state/store';
import type { Bvh } from './bvh';
import type { MeshData, MeshIn, MeshOut } from './mesh.worker';
import type { ShellsIn, ShellsOut } from './shells.worker';
import type { ProjectItem, ProjectMaterials } from './threemf';

const DEG = Math.PI / 180;

let worker: Worker | null = null;
let nextRequest = 1;
const pending = new Map<number, (m: MeshOut) => void>();

export function request(msg: DistributiveOmit<MeshIn, 'id'>, on: (m: MeshOut) => void, transfer: Transferable[] = []) {
  if (!worker) {
    worker = new Worker(new URL('./mesh.worker.ts', import.meta.url), { type: 'module' });
    worker.onmessage = (e: MessageEvent<MeshOut>) => {
      const h = pending.get(e.data.id);
      if (e.data.type === 'done' || e.data.type === 'error') pending.delete(e.data.id);
      h?.(e.data);
    };
  }
  const id = nextRequest++;
  pending.set(id, on);
  worker.postMessage({ ...msg, id }, transfer);
}
type DistributiveOmit<T, K extends keyof T> = T extends unknown ? Omit<T, K> : never;

function toGeometry(d: MeshData): THREE.BufferGeometry {
  const geo = new THREE.BufferGeometry();
  geo.userData.sourceCentre = d.centre;
  geo.setAttribute('position', new THREE.BufferAttribute(d.positions, 3));
  geo.setAttribute('normal', new THREE.BufferAttribute(d.normals, 3));
  geo.setIndex(new THREE.BufferAttribute(d.index, 1));
  geo.boundingBox = new THREE.Box3(new THREE.Vector3(...d.min), new THREE.Vector3(...d.max));
  geo.boundingSphere = new THREE.Sphere(geo.boundingBox.getCenter(new THREE.Vector3()), d.radius);
  checkShells(geo);
  return geo;
}

let shellsWorker: Worker | null = null;
const shellsPending = new Map<number, THREE.BufferGeometry>();
let shellsChecked: ((geo: THREE.BufferGeometry) => void) | null = null;
export const onShellsChecked = (f: (geo: THREE.BufferGeometry) => void) => void (shellsChecked = f);

function checkShells(geo: THREE.BufferGeometry) {
  if (!shellsWorker) {
    shellsWorker = new Worker(new URL('./shells.worker.ts', import.meta.url), { type: 'module' });
    shellsWorker.onmessage = (e: MessageEvent<ShellsOut>) => {
      const g = shellsPending.get(e.data.id);
      shellsPending.delete(e.data.id);
      if (!g) return;
      g.userData.printInPlaceGap = e.data.gap;
      shellsChecked?.(g);
    };
  }
  const id = nextRequest++;
  shellsPending.set(id, geo);
  shellsWorker.postMessage({ id, positions: geo.attributes.position.array as Float32Array, index: geo.index!.array as Uint32Array } satisfies ShellsIn);
}

function setBvh(geo: THREE.BufferGeometry, m: { bvh: Bvh; ms: number }) {
  geo.userData.bvh = m.bvh;
  geo.userData.bvhMs = m.ms;
}

export function loadMesh(file: File, progress?: (pct: number) => void): Promise<THREE.BufferGeometry> {
  return new Promise((resolve, reject) => {
    let geo: THREE.BufferGeometry | null = null;
    request({ type: 'mesh', file }, (m) => {
      if (m.type === 'progress') progress?.(m.pct);
      else if (m.type === 'mesh') resolve((geo = toGeometry(m)));
      else if (m.type === 'hull' && geo) geo.userData.hull = m.hull;
      else if (m.type === 'bvh' && geo) setBvh(geo, m);
      else if (m.type === 'error') reject(new Error(m.message));
    });
  });
}

export function loadProject(file: File, on: {
  items(items: ProjectItem[], materials: ProjectMaterials): void;
  mesh(i: number, geometry: THREE.BufferGeometry, centre: THREE.Vector3): void;
  missing(i: number): void;
}): Promise<void> {
  return new Promise((resolve, reject) => {
    const geos = new Map<number, THREE.BufferGeometry>();
    request({ type: 'project', file }, (m) => {
      if (m.type === 'items') on.items(m.items, m.materials);
      else if (m.type === 'mesh') {
        const geo = toGeometry(m);
        geos.set(m.item!, geo);
        on.mesh(m.item!, geo, new THREE.Vector3(...m.centre));
      } else if (m.type === 'hull') {
        const geo = geos.get(m.item!);
        if (geo) geo.userData.hull = m.hull;
      } else if (m.type === 'bvh') {
        const geo = geos.get(m.item!);
        if (geo) setBvh(geo, m);
      } else if (m.type === 'missing') on.missing(m.item);
      else if (m.type === 'done') resolve();
      else if (m.type === 'error') reject(new Error(m.message));
    });
  });
}

export function placeholderGeometry(size = 20): THREE.BufferGeometry {
  const geo = new THREE.BoxGeometry(size, size, size).translate(0, 0, size / 2);
  geo.computeBoundingBox();
  geo.computeBoundingSphere();
  return geo;
}

function tiltScale(o: PlateObject): THREE.Matrix4 {
  const m = new THREE.Matrix4().makeScale(...o.scale);
  m.premultiply(new THREE.Matrix4().makeRotationX(o.rotX * DEG));
  m.premultiply(new THREE.Matrix4().makeRotationY(o.rotY * DEG));
  return m;
}

function boxCorners(b: THREE.Box3): Float32Array {
  const out = new Float32Array(24);
  for (let i = 0; i < 8; i++) out.set([i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y, i & 4 ? b.max.z : b.min.z], 3 * i);
  return out;
}

const boxCache = new WeakMap<THREE.BufferGeometry, Map<string, THREE.Box3>>();
function transformedBox(geo: THREE.BufferGeometry, m: THREE.Matrix4, key: string): THREE.Box3 {
  let per = boxCache.get(geo);
  if (!per) boxCache.set(geo, (per = new Map()));
  let b = per.get(key);
  if (!b) {
    const e = m.elements;
    const axisAligned = [0, 4, 8].every((c) => [e[c], e[c + 1], e[c + 2]].filter((v) => Math.abs(v) > 1e-12).length === 1);
    const p = (geo.userData.hull as Float32Array | undefined) ??
      (axisAligned && geo.boundingBox ? boxCorners(geo.boundingBox) : (geo.attributes.position.array as Float32Array));
    let x0 = Infinity, y0 = Infinity, z0 = Infinity, x1 = -Infinity, y1 = -Infinity, z1 = -Infinity;
    for (let i = 0; i < p.length; i += 3) {
      const x = p[i], y = p[i + 1], z = p[i + 2];
      const tx = e[0] * x + e[4] * y + e[8] * z + e[12], ty = e[1] * x + e[5] * y + e[9] * z + e[13], tz = e[2] * x + e[6] * y + e[10] * z + e[14];
      if (tx < x0) x0 = tx;
      if (tx > x1) x1 = tx;
      if (ty < y0) y0 = ty;
      if (ty > y1) y1 = ty;
      if (tz < z0) z0 = tz;
      if (tz > z1) z1 = tz;
    }
    b = new THREE.Box3(new THREE.Vector3(x0, y0, z0), new THREE.Vector3(x1, y1, z1));
    if (per.size > 64) per.clear();
    per.set(key, b);
  }
  return b;
}

export function objectMatrix(o: PlateObject): THREE.Matrix4 {
  const m = tiltScale(o);
  const b = transformedBox(o.geometry, m, `${o.rotX},${o.rotY},${o.scale}`);
  return m.premultiply(new THREE.Matrix4().makeTranslation(-(b.min.x + b.max.x) / 2, -(b.min.y + b.max.y) / 2, -b.min.z));
}

export function instanceMatrix(o: PlateObject, inst: Instance): THREE.Matrix4 {
  const m = objectMatrix(o).premultiply(new THREE.Matrix4().makeRotationZ(inst.rotZ * DEG));
  const b = transformedBox(o.geometry, m, `${o.rotX},${o.rotY},${o.scale},${inst.rotZ}`);
  return m.premultiply(new THREE.Matrix4().makeTranslation(inst.x - (b.min.x + b.max.x) / 2, inst.y - (b.min.y + b.max.y) / 2, inst.z ?? 0));
}

export function instanceBox(o: PlateObject, inst: Instance): THREE.Box3 {
  const m = objectMatrix(o).premultiply(new THREE.Matrix4().makeRotationZ(inst.rotZ * DEG));
  const b = transformedBox(o.geometry, m, `${o.rotX},${o.rotY},${o.scale},${inst.rotZ}`).clone();
  const c = new THREE.Vector3((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, 0);
  b.translate(new THREE.Vector3(inst.x, inst.y, inst.z ?? 0).sub(c));
  for (const v of o.volumes ?? []) if (v.type === 'part') b.union(volumeBox(o, inst, v));
  return b;
}

export function volumeWorld(o: PlateObject, inst: Instance, v: Volume): THREE.Matrix4 {
  return instanceMatrix(o, inst).multiply(new THREE.Matrix4().fromArray(v.matrix));
}

export function volumeBox(o: PlateObject, inst: Instance, v: Volume): THREE.Box3 {
  const m = volumeWorld(o, inst, v);
  return transformedBox(v.geometry, m, m.elements.join());
}

export function volumeGeometry(position: Float32Array, index: Uint32Array | Uint16Array | null, normal?: Float32Array): THREE.BufferGeometry {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(position, 3));
  if (index) geo.setIndex(new THREE.BufferAttribute(index, 1));
  if (normal) geo.setAttribute('normal', new THREE.BufferAttribute(normal, 3));
  else geo.computeVertexNormals();
  geo.computeBoundingBox();
  const c = geo.boundingBox!.getCenter(new THREE.Vector3());
  if (c.lengthSq() > 0) {
    geo.translate(-c.x, -c.y, -c.z);
    geo.computeBoundingBox();
  }
  geo.computeBoundingSphere();
  return geo;
}

export function primitiveGeometry(shape: 'Cube' | 'Cylinder' | 'Sphere' | 'Cone' | 'Disc' | 'Torus', side: number): THREE.BufferGeometry {
  const r = side / 2;
  const g = shape === 'Cube' ? new THREE.BoxGeometry(side, side, side)
    : shape === 'Cylinder' ? new THREE.CylinderGeometry(r, r, side, 120).rotateX(Math.PI / 2)
    : shape === 'Sphere' ? new THREE.SphereGeometry(r, 90, 45).rotateX(Math.PI / 2)
    : shape === 'Cone' ? new THREE.ConeGeometry(r, side, 120).rotateX(Math.PI / 2)
    : shape === 'Disc' ? new THREE.CylinderGeometry(r, r, 0.2, 120).rotateX(Math.PI / 2)
    : new THREE.TorusGeometry(r, side / 8, 60, 120);
  const welded = mergeVertices(new THREE.BufferGeometry().setAttribute('position', g.getAttribute('position')).setIndex(g.index), 1e-4);
  g.dispose();
  return volumeGeometry(welded.attributes.position.array as Float32Array, welded.index!.array as Uint32Array);
}

export function objectSize(o: PlateObject): THREE.Vector3 {
  const b = transformedBox(o.geometry, tiltScale(o), `${o.rotX},${o.rotY},${o.scale}`);
  return b.getSize(new THREE.Vector3());
}

export function bakeStl(o: PlateObject, matrix = objectMatrix(o)): Promise<Blob> {
  const pos = o.geometry.attributes.position.array as Float32Array;
  const index = (o.geometry.index?.array as Uint32Array | Uint16Array | undefined) ?? null;
  return new Promise((resolve, reject) =>
    request({ type: 'bake', positions: pos, index, matrix: matrix.elements }, (m) => {
      if (m.type === 'blob') resolve(m.blob);
      else if (m.type === 'error') reject(new Error(m.message));
    }),
  );
}

function bakeVolume(o: PlateObject, v: Volume): Promise<Blob> {
  const index = (v.geometry.index?.array as Uint32Array | Uint16Array | undefined) ?? null;
  return new Promise((resolve, reject) =>
    request({ type: 'bake', positions: v.geometry.attributes.position.array as Float32Array, index,
      matrix: objectMatrix(o).multiply(new THREE.Matrix4().fromArray(v.matrix)).elements }, (m) => {
      if (m.type === 'blob') resolve(m.blob);
      else if (m.type === 'error') reject(new Error(m.message));
    }),
  );
}

export function plateDescription(objects: PlateObject[]): PlateJson {
  const used = new Set<string>();
  const plate: PlateJson = { objects: [] };
  for (const o of objects) {
    let file = `${o.name.replace(/[^A-Za-z0-9_.-]+/g, '_').replace(/\.(stl|obj)$/i, '')}.stl`;
    for (let k = 2; used.has(file); k++) file = file.replace(/(_\d+)?\.stl$/, `_${k}.stl`);
    used.add(file);
    const settings = (c: PlateObject['settings']) => Object.fromEntries(Object.entries(c).map(([k, v]) => [k, toolValue(k, v)]));
    const place = (i: Instance) => {
      if (!o.volumes?.some((v) => v.type === 'part')) return { x: i.x, y: i.y, z: i.z ?? 0 };
      const b = instanceBox(o, i);
      return { x: (b.min.x + b.max.x) / 2, y: (b.min.y + b.max.y) / 2, z: b.min.z };
    };
    plate.objects.push({
      file,
      name: o.name.replace(/\.(stl|obj)$/i, ''),
      extruder: o.extruder,
      instances: o.instances.map((i) => { const p = place(i); return { x: round(p.x), y: round(p.y), rot: round(i.rotZ), z: round(p.z), printable: i.printable !== false }; }),
      ...(Object.keys(o.settings).length ? { settings: settings(o.settings) } : {}),
      ...(o.volumes?.length ? { volumes: o.volumes.map((v, k) => ({
        file: file.replace(/\.stl$/, `.v${k + 1}.stl`),
        type: v.type,
        name: v.name,
        ...(v.extruder ? { extruder: v.extruder } : {}),
        ...(Object.keys(v.settings).length ? { settings: settings(v.settings) } : {}),
      })) } : {}),
    });
  }
  return plate;
}

export async function plateJson(objects: PlateObject[]): Promise<{ plate: PlateJson; files: { name: string; blob: Blob }[] }> {
  const plate = plateDescription(objects);
  const blobs = await Promise.all(objects.map((o) => bakeStl(o)));
  const volumes = await Promise.all(objects.map((o) => Promise.all((o.volumes ?? []).map((v) => bakeVolume(o, v)))));
  return { plate, files: plate.objects.flatMap(({ file: name, volumes: vs }, i) => [{ name, blob: blobs[i] },
    ...(vs ?? []).map((v, k) => ({ name: v.file, blob: volumes[i][k] }))]) };
}

const meshContents = new WeakMap<THREE.BufferGeometry, string>();
export function meshSignature(o: PlateObject): unknown[] {
  let contents = meshContents.get(o.geometry);
  if (contents === undefined) {
    contents = JSON.stringify([
      Array.from(o.geometry.attributes.position.array),
      o.geometry.index && Array.from(o.geometry.index.array),
    ]);
    meshContents.set(o.geometry, contents);
  }
  return [contents, objectMatrix(o).elements, ...(o.volumes ?? []).map((v) => [meshSignature({ ...o, geometry: v.geometry, volumes: [] })[0], v.matrix])];
}

const round = (v: number) => Math.round(v * 1000) / 1000;

export function parsePoints(v: ConfigValue | undefined): THREE.Vector2[] {
  const list = Array.isArray(v) ? v.map(String) : typeof v === 'string' ? v.split(/[,;]/) : [];
  return list
    .map((s) => s.split('x').map(Number))
    .filter((p) => p.length === 2 && p.every(Number.isFinite))
    .map(([x, y]) => new THREE.Vector2(x, y));
}

export function bedBounds(area: THREE.Vector2[]): { min: THREE.Vector2; max: THREE.Vector2; center: THREE.Vector2 } {
  const min = new THREE.Vector2(Infinity, Infinity), max = new THREE.Vector2(-Infinity, -Infinity);
  for (const p of area) {
    min.min(p);
    max.max(p);
  }
  if (!area.length) min.set(0, 0), max.set(200, 200);
  return { min, max, center: min.clone().add(max).multiplyScalar(0.5) };
}

export function arrange(objects: PlateObject[], area: THREE.Vector2[], spacing = 6): PlateObject[] {
  const { min, max, center } = bedBounds(area);
  const width = max.x - min.x;
  const items = objects.flatMap((o) =>
    o.instances.map((i) => {
      const s = instanceBox(o, i).getSize(new THREE.Vector3());
      return { o, i, w: s.x, d: s.y };
    }),
  );
  items.sort((a, b) => b.w * b.d - a.w * a.d);
  const rows: { items: typeof items; w: number; d: number }[] = [];
  for (const it of items) {
    const row = rows.find((r) => r.w + spacing + it.w <= width);
    if (row) {
      row.items.push(it);
      row.w += spacing + it.w;
      row.d = Math.max(row.d, it.d);
    } else rows.push({ items: [it], w: it.w, d: it.d });
  }
  const totalD = rows.reduce((n, r) => n + r.d, 0) + spacing * Math.max(0, rows.length - 1);
  const placed = new Map<number, { x: number; y: number }>();
  let y = center.y + totalD / 2;
  for (const r of rows) {
    let x = center.x - r.w / 2;
    for (const it of r.items) {
      placed.set(it.i.id, { x: x + it.w / 2, y: y - r.d / 2 });
      x += it.w + spacing;
    }
    y -= r.d + spacing;
  }
  return objects.map((o) => ({ ...o, instances: o.instances.map((i) => ({ ...i, ...(placed.get(i.id) ?? {}) })) }));
}

export function nextFreeSpot(objects: PlateObject[], o: PlateObject, area: THREE.Vector2[]): { x: number; y: number } | null {
  const { min, max } = bedBounds(area);
  const size = objectSize(o);
  const boxes = objects.flatMap((ob) => ob.instances.map((i) => instanceBox(ob, i)));
  const step = Math.max(8, Math.min(size.x, size.y) / 4);
  for (let y = max.y - size.y / 2 - 2; y >= min.y + size.y / 2; y -= step)
    for (let x = min.x + size.x / 2 + 2; x <= max.x - size.x / 2; x += step) {
      const cand = new THREE.Box3(new THREE.Vector3(x - size.x / 2 - 3, y - size.y / 2 - 3, -1), new THREE.Vector3(x + size.x / 2 + 3, y + size.y / 2 + 3, 1e4));
      if (!boxes.some((b) => b.intersectsBox(cand))) return { x, y };
    }
  return null;
}

export function nearestFreeSpot(objects: PlateObject[], size: THREE.Vector2, from: THREE.Vector2, area: THREE.Vector2[]): THREE.Vector2 | null {
  const { min, max } = bedBounds(area);
  const boxes = objects.flatMap((ob) => ob.instances.map((i) => instanceBox(ob, i)));
  const step = Math.max(2, Math.min(size.x, size.y) / 4);
  const hx = size.x / 2 + 1, hy = size.y / 2 + 1;
  let best: THREE.Vector2 | null = null;
  for (let y = min.y + hy; y <= max.y - hy; y += step)
    for (let x = min.x + hx; x <= max.x - hx; x += step) {
      const p = new THREE.Vector2(x, y);
      if (best && p.distanceTo(from) >= best.distanceTo(from)) continue;
      const cand = new THREE.Box3(new THREE.Vector3(x - hx, y - hy, -1), new THREE.Vector3(x + hx, y + hy, 1e4));
      if (!boxes.some((b) => b.intersectsBox(cand))) best = p;
    }
  return best;
}

export function outsideBed(objects: PlateObject[], area: THREE.Vector2[], height: number): Set<number> {
  const out = new Set<number>();
  const poly = area;
  const inside = (x: number, y: number) => {
    let c = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
      const a = poly[i], b = poly[j];
      const dx = b.x - a.x, dy = b.y - a.y;
      const t = Math.max(0, Math.min(1, ((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy)));
      if (Math.hypot(x - a.x - t * dx, y - a.y - t * dy) <= 1e-3) return true;
      if (a.y > y !== b.y > y && x < ((b.x - a.x) * (y - a.y)) / (b.y - a.y) + a.x) c = !c;
    }
    return c;
  };
  for (const o of objects)
    for (const i of o.instances) {
      if (i.printable === false) continue;
      const b = instanceBox(o, i);
      const corners = [[b.min.x, b.min.y], [b.max.x, b.min.y], [b.max.x, b.max.y], [b.min.x, b.max.y]];
      if (b.max.z > height + 1e-3 || !corners.every(([x, y]) => inside(x, y))) out.add(i.id);
    }
  return out;
}
