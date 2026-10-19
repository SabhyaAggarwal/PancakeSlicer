import * as THREE from 'three';
import type { Instance, PlateObject } from '../state/store';
import { instanceMatrix } from './geometry';

function projectTransform(o: PlateObject, inst: Instance): number[] | undefined {
  if (!o.project?.origin) return undefined;
  const [x, y, z] = o.project.origin;
  const delta = instanceMatrix(o, inst).multiply(new THREE.Matrix4().makeTranslation(-x, -y, -z));
  const [px, py] = o.project.plateOrigin ?? [0, 0];
  const e = new THREE.Matrix4().makeTranslation(px, py, 0).multiply(delta).multiply(new THREE.Matrix4().makeTranslation(-px, -py, 0)).elements;
  return [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14].map((i) => e[i]);
}

export function projectInstances(objects: PlateObject[]) {
  return objects.flatMap((o) => o.instances.map((i) => ({ printable: i.printable !== false, transform: projectTransform(o, i) })));
}
