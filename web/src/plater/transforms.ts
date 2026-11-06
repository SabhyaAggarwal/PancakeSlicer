



import * as THREE from 'three';
import { ConvexHull } from 'three/examples/jsm/math/ConvexHull.js';
import type { Instance, PlateObject } from '../state/store';

const DEG = Math.PI / 180;


function rotation(o: PlateObject, inst: Instance): THREE.Matrix4 {
  return new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(o.rotX * DEG, o.rotY * DEG, inst.rotZ * DEG, 'ZYX'));
}


export function linearOf(o: PlateObject, inst: Instance): THREE.Matrix4 {
  return rotation(o, inst).multiply(new THREE.Matrix4().makeScale(...o.scale));
}

const wrap = (deg: number) => {
  const d = ((deg + 180) % 360 + 360) % 360 - 180;
  return Math.abs(d) < 1e-9 ? 0 : d;
};






function withRotation(o: PlateObject, instId: number, r: THREE.Matrix4, scale = o.scale): PlateObject {
  const sel = o.instances.find((i) => i.id === instId) ?? o.instances[0];
  const e = new THREE.Euler().setFromRotationMatrix(r, 'ZYX');
  const rotZ = wrap(e.z / DEG);
  return {
    ...o, rotX: wrap(e.x / DEG), rotY: wrap(e.y / DEG), scale,
    instances: o.instances.map((i) => ({ ...i, rotZ: i.id === sel.id ? rotZ : wrap(i.rotZ + rotZ - sel.rotZ) })),
  };
}


export function rotateWorld(o: PlateObject, instId: number, q: THREE.Matrix4): PlateObject {
  const sel = o.instances.find((i) => i.id === instId) ?? o.instances[0];
  return withRotation(o, instId, q.clone().multiply(rotation(o, sel)));
}





export function mirror(o: PlateObject, instId: number, axis: 0 | 1 | 2): PlateObject {
  const sel = o.instances.find((i) => i.id === instId) ?? o.instances[0];
  const m = new THREE.Matrix4().makeScale(axis === 0 ? -1 : 1, axis === 1 ? -1 : 1, axis === 2 ? -1 : 1);
  const scale = o.scale.map((v, k) => (k === axis ? -v : v)) as [number, number, number];
  return withRotation(o, instId, m.clone().multiply(rotation(o, sel)).multiply(m), scale);
}



export interface LayOnFacePlane {

  normal: THREE.Vector3;

  area: number;

  outline: THREE.Vector3[];

  down: number;
}


function hull2(points: THREE.Vector3[]): THREE.Vector3[] {
  const p = [...points].sort((a, b) => a.x - b.x || a.y - b.y);
  if (p.length < 3) return p;
  const cross = (o: THREE.Vector3, a: THREE.Vector3, b: THREE.Vector3) => (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  const lower: THREE.Vector3[] = [], upper: THREE.Vector3[] = [];
  for (const v of p) {
    while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], v) <= 0) lower.pop();
    lower.push(v);
  }
  for (let i = p.length - 1; i >= 0; i--) {
    while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p[i]) <= 0) upper.pop();
    upper.push(p[i]);
  }
  return lower.slice(0, -1).concat(upper.slice(0, -1));
}






export function layOnFacePlanes(points: ArrayLike<number>, linear: THREE.Matrix4): LayOnFacePlane[] {
  const vs: THREE.Vector3[] = [];
  for (let i = 0; i < points.length; i += 3) vs.push(new THREE.Vector3(points[i], points[i + 1], points[i + 2]));
  if (vs.length < 4) return [];
  const faces = new ConvexHull().setFromPoints(vs).faces;
  const normalMatrix = new THREE.Matrix3().getNormalMatrix(linear);
  const visited = new Set<(typeof faces)[number]>();
  const planes: LayOnFacePlane[] = [];
  const minimalArea = 5, minimalSide = 1, angleThreshold = Math.cos(1 * DEG);
  for (const start of faces) {
    if (visited.has(start)) continue;

    const n0 = start.normal.clone();
    const pts: THREE.Vector3[] = [];
    const queue = [start];
    visited.add(start);
    while (queue.length) {
      const f = queue.pop()!;
      let e = f.edge;
      do {
        pts.push(e.head().point);
        const twin = e.twin?.face;
        if (twin && !visited.has(twin) && Math.abs(twin.normal.x - n0.x) < 1e-3 && Math.abs(twin.normal.y - n0.y) < 1e-3 && Math.abs(twin.normal.z - n0.z) < 1e-3) {
          visited.add(twin);
          queue.push(twin);
        }
        e = e.next;
      } while (e !== f.edge);
    }
    const world = pts.map((p) => p.clone().applyMatrix4(linear));
    if (world.length === 3 && (world[0].distanceTo(world[1]) < minimalSide || world[0].distanceTo(world[2]) < minimalSide || world[1].distanceTo(world[2]) < minimalSide)) continue;

    const tn = n0.clone().applyMatrix3(normalMatrix).normalize();
    const toPlane = new THREE.Quaternion().setFromUnitVectors(tn, new THREE.Vector3(0, 0, 1));
    const flat = hull2(world.map((p) => p.clone().applyQuaternion(toPlane)));
    let area = 0;
    for (let i = 0; i < flat.length; i++) {
      const a = flat[i], b = flat[(i + 1) % flat.length];
      area += a.x * b.y - b.x * a.y;
    }
    area = Math.abs(area) / 2;
    if (area < minimalArea) continue;
    const sharp = flat.some((cur, i) => {
      const prev = flat[(i + flat.length - 1) % flat.length], next = flat[(i + 1) % flat.length];
      return prev.clone().sub(cur).normalize().dot(next.clone().sub(cur).normalize()) > angleThreshold;
    });
    if (sharp) continue;
    const back = toPlane.clone().invert();
    const inverse = linear.clone().invert();
    planes.push({ normal: n0, area, outline: flat.map((p) => p.clone().applyQuaternion(back).applyMatrix4(inverse)), down: tn.z });
  }
  return planes.sort((a, b) => b.area - a.area);
}


export function largestPlane(planes: LayOnFacePlane[]): number {
  let best = -1;
  for (let i = 0; i < planes.length && planes[i].area >= planes[0].area * (1 - 1e-3); i++)
    if (best < 0 || planes[i].down < planes[best].down) best = i;
  return best;
}





export function layOnFace(o: PlateObject, instId: number, normal: THREE.Vector3): PlateObject {
  const sel = o.instances.find((i) => i.id === instId) ?? o.instances[0];
  const tn = normal.clone().applyMatrix3(new THREE.Matrix3().getNormalMatrix(linearOf(o, sel))).normalize();

  const q = tn.z > 1 - 1e-12 ? new THREE.Matrix4().makeRotationX(Math.PI)
    : new THREE.Matrix4().makeRotationFromQuaternion(new THREE.Quaternion().setFromUnitVectors(tn, new THREE.Vector3(0, 0, -1)));
  const next = rotateWorld(o, instId, q);
  return { ...next, instances: next.instances.map((i) => ({ ...i, z: 0 })) };
}
