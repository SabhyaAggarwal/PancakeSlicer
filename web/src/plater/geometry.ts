import * as THREE from 'three';
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
import { OBJLoader } from 'three/examples/jsm/loaders/OBJLoader.js';
import { mergeVertices } from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import type { ConfigValue, PlateJson } from '../api/types';
import { toolValue } from '../options';
import type { Instance, PlateObject } from '../state/store';

const DEG = Math.PI / 180;

export async function loadMesh(file: File): Promise<THREE.BufferGeometry> {
  const ext = file.name.toLowerCase().split('.').pop();
  let geo: THREE.BufferGeometry;
  if (ext === 'stl') {
    geo = new STLLoader().parse(await file.arrayBuffer());
  } else if (ext === 'obj') {
    const group = new OBJLoader().parse(await file.text());
    const parts: THREE.BufferGeometry[] = [];
    group.traverse((o) => {
      if ((o as THREE.Mesh).isMesh) {
        const g = (o as THREE.Mesh).geometry.clone();
        g.deleteAttribute('normal');
        g.deleteAttribute('uv');
        parts.push(g.index ? g.toNonIndexed() : g);
      }
    });
    if (!parts.length) throw new Error('no mesh in ' + file.name);
    geo = parts.length === 1 ? parts[0] : mergeGeoms(parts);
  } else {
    throw new Error(`${file.name}: STL and OBJ load in the browser; 3MF needs the server loader`);
  }
  return finishMesh(geo).geometry;
}

export function finishMesh(geo: THREE.BufferGeometry): { geometry: THREE.BufferGeometry; centre: THREE.Vector3 } {
  if (geo.getAttribute('normal')) geo.deleteAttribute('normal');
  geo = mergeVertices(geo, 1e-4);
  geo.computeVertexNormals();
  geo.computeBoundingBox();
  const b = geo.boundingBox!;
  const centre = new THREE.Vector3((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, b.min.z);
  geo.translate(-centre.x, -centre.y, -centre.z);
  geo.computeBoundingBox();
  geo.computeBoundingSphere();
  return { geometry: geo, centre };
}

export function meshFromPositions(positions: Float32Array): { geometry: THREE.BufferGeometry; centre: THREE.Vector3 } {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  return finishMesh(geo);
}

export function placeholderGeometry(size = 20): THREE.BufferGeometry {
  const geo = new THREE.BoxGeometry(size, size, size).translate(0, 0, size / 2);
  geo.computeBoundingBox();
  geo.computeBoundingSphere();
  return geo;
}

function mergeGeoms(parts: THREE.BufferGeometry[]): THREE.BufferGeometry {
  const total = parts.reduce((n, g) => n + g.attributes.position.count, 0);
  const pos = new Float32Array(total * 3);
  let off = 0;
  for (const g of parts) {
    pos.set(g.attributes.position.array as Float32Array, off);
    off += g.attributes.position.count * 3;
  }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  return out;
}

function tiltScale(o: PlateObject): THREE.Matrix4 {
  const m = new THREE.Matrix4().makeScale(...o.scale);
  m.premultiply(new THREE.Matrix4().makeRotationX(o.rotX * DEG));
  m.premultiply(new THREE.Matrix4().makeRotationY(o.rotY * DEG));
  return m;
}

const boxCache = new WeakMap<THREE.BufferGeometry, Map<string, THREE.Box3>>();
function transformedBox(geo: THREE.BufferGeometry, m: THREE.Matrix4, key: string): THREE.Box3 {
  let per = boxCache.get(geo);
  if (!per) boxCache.set(geo, (per = new Map()));
  let b = per.get(key);
  if (!b) {
    b = new THREE.Box3();
    const p = geo.attributes.position as THREE.BufferAttribute;
    const v = new THREE.Vector3();
    for (let i = 0; i < p.count; i++) b.expandByPoint(v.fromBufferAttribute(p, i).applyMatrix4(m));
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
  return m.premultiply(new THREE.Matrix4().makeTranslation(inst.x - (b.min.x + b.max.x) / 2, inst.y - (b.min.y + b.max.y) / 2, 0));
}

export function instanceBox(o: PlateObject, inst: Instance): THREE.Box3 {
  const m = objectMatrix(o).premultiply(new THREE.Matrix4().makeRotationZ(inst.rotZ * DEG));
  const b = transformedBox(o.geometry, m, `${o.rotX},${o.rotY},${o.scale},${inst.rotZ}`).clone();
  const c = new THREE.Vector3((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, 0);
  return b.translate(new THREE.Vector3(inst.x, inst.y, 0).sub(c));
}

export function objectSize(o: PlateObject): THREE.Vector3 {
  const b = transformedBox(o.geometry, tiltScale(o), `${o.rotX},${o.rotY},${o.scale}`);
  return b.getSize(new THREE.Vector3());
}

export function bakeStl(o: PlateObject): Blob {
  const m = objectMatrix(o);
  const geo = o.geometry.index ? o.geometry.toNonIndexed() : o.geometry;
  const p = geo.attributes.position as THREE.BufferAttribute;
  const tris = p.count / 3;
  const buf = new ArrayBuffer(84 + tris * 50);
  const dv = new DataView(buf);
  new TextEncoder().encodeInto('OrcaSlicer web plate object', new Uint8Array(buf, 0, 80));
  dv.setUint32(80, tris, true);
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), n = new THREE.Vector3();
  let off = 84;
  for (let t = 0; t < tris; t++) {
    a.fromBufferAttribute(p, 3 * t).applyMatrix4(m);
    b.fromBufferAttribute(p, 3 * t + 1).applyMatrix4(m);
    c.fromBufferAttribute(p, 3 * t + 2).applyMatrix4(m);
    n.subVectors(c, b).cross(new THREE.Vector3().subVectors(a, b)).normalize();
    for (const v of [n, a, b, c]) {
      dv.setFloat32(off, v.x, true);
      dv.setFloat32(off + 4, v.y, true);
      dv.setFloat32(off + 8, v.z, true);
      off += 12;
    }
    off += 2;
  }
  return new Blob([buf], { type: 'model/stl' });
}

export function plateJson(objects: PlateObject[]): { plate: PlateJson; files: { name: string; blob: Blob }[] } {
  const files: { name: string; blob: Blob }[] = [];
  const used = new Set<string>();
  const plate: PlateJson = { objects: [] };
  for (const o of objects) {
    let file = `${o.name.replace(/[^A-Za-z0-9_.-]+/g, '_').replace(/\.(stl|obj)$/i, '')}.stl`;
    for (let k = 2; used.has(file); k++) file = file.replace(/(_\d+)?\.stl$/, `_${k}.stl`);
    used.add(file);
    files.push({ name: file, blob: bakeStl(o) });
    plate.objects.push({
      file,
      name: o.name.replace(/\.(stl|obj)$/i, ''),
      extruder: o.extruder,
      instances: o.instances.map((i) => ({ x: round(i.x), y: round(i.y), rot: round(i.rotZ) })),
      ...(Object.keys(o.settings).length
        ? { settings: Object.fromEntries(Object.entries(o.settings).map(([k, v]) => [k, toolValue(k, v)])) }
        : {}),
    });
  }
  return { plate, files };
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

export function outsideBed(objects: PlateObject[], area: THREE.Vector2[], height: number): Set<number> {
  const out = new Set<number>();
  const poly = area;
  const inside = (x: number, y: number) => {
    let c = false;
    for (let i = 0, j = poly.length - 1; i < poly.length; j = i++)
      if (poly[i].y > y !== poly[j].y > y && x < ((poly[j].x - poly[i].x) * (y - poly[i].y)) / (poly[j].y - poly[i].y) + poly[i].x) c = !c;
    return c;
  };
  for (const o of objects)
    for (const i of o.instances) {
      const b = instanceBox(o, i);
      const corners = [[b.min.x, b.min.y], [b.max.x, b.min.y], [b.max.x, b.max.y], [b.min.x, b.max.y]];
      if (b.max.z > height + 1e-3 || !corners.every(([x, y]) => inside(x, y))) out.add(i.id);
    }
  return out;
}
