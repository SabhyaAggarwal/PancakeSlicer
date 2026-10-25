import { Euler, Matrix4, Quaternion, Vector3 } from 'three';
import type { PlateObject } from '../state/store';

const DEG = Math.PI / 180;

export function withRotation(o: PlateObject, rotation: number[]): PlateObject {
  const r = new Matrix4().set(
    rotation[0], rotation[1], rotation[2], 0,
    rotation[3], rotation[4], rotation[5], 0,
    rotation[6], rotation[7], rotation[8], 0, 0, 0, 0, 1,
  );
  r.multiply(new Matrix4().makeRotationY(o.rotY * DEG)).multiply(new Matrix4().makeRotationX(o.rotX * DEG));
  const angles = new Euler().setFromRotationMatrix(r, 'ZYX');
  return { ...o, rotX: angles.x / DEG, rotY: angles.y / DEG,
    instances: o.instances.map((i) => ({ ...i, rotZ: i.rotZ + angles.z / DEG, z: 0 })) };
}

export function rotationToZ(up: [number, number, number]): number[] {
  const u = new Vector3(...up).normalize();
  const flip = new Matrix4();
  if (u.z < 0) {
    flip.makeRotationX(Math.PI);
    u.applyMatrix4(flip);
  }
  const m = new Matrix4().makeRotationFromQuaternion(new Quaternion().setFromUnitVectors(u, new Vector3(0, 0, 1))).multiply(flip);
  const e = m.elements;
  return [e[0], e[4], e[8], e[1], e[5], e[9], e[2], e[6], e[10]];
}
