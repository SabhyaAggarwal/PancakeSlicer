import { Euler, Matrix4 } from 'three';
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
