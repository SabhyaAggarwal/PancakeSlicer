


import * as THREE from 'three';
import { getState, markStale, newId, setState, type PlateObject, type Volume } from '../state/store';
import { undoable } from '../state/history';
import { bakeStl, instanceMatrix, volumeGeometry } from './geometry';

const copy = (g: THREE.BufferGeometry, m: THREE.Matrix4) => {
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute((g.attributes.position.array as Float32Array).slice(), 3));
  if (g.index) out.setIndex(new THREE.BufferAttribute((g.index.array as Uint32Array).slice(), 1));
  return out.applyMatrix4(m);
};

export async function assemble(ids: number[]) {
  const objects = getState().objects.filter((o) => ids.includes(o.id) && !o.project?.placeholder);
  if (objects.length < 2 && !(objects.length === 1 && objects[0].instances.length > 1)) return;
  const [o0] = objects, i0 = o0.instances[0];

  const world0 = copy(o0.geometry, instanceMatrix(o0, i0));
  world0.computeBoundingBox();
  const b = world0.boundingBox!, c = new THREE.Vector3((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, b.min.z);
  const back = new THREE.Matrix4().makeTranslation(-c.x, -c.y, -c.z);
  const geometry = world0.applyMatrix4(back);
  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();
  const volumes: Volume[] = [];
  for (const o of objects)
    for (const [k, inst] of o.instances.entries()) {
      const placed = back.clone().multiply(instanceMatrix(o, inst));
      if (o !== o0 || k > 0) {
        const g = copy(o.geometry, new THREE.Matrix4());
        g.computeBoundingBox();
        const d = g.boundingBox!.getCenter(new THREE.Vector3());
        const centred = volumeGeometry(g.attributes.position.array as Float32Array, (g.index?.array as Uint32Array | undefined) ?? null);
        volumes.push({ id: newId(), name: o.name, type: 'part', geometry: centred, matrix: placed.clone().multiply(new THREE.Matrix4().makeTranslation(d.x, d.y, d.z)).elements,
          extruder: o.extruder, settings: { ...o.settings } });
      }
      for (const v of o.volumes ?? [])
        volumes.push({ ...v, id: newId(), paint: undefined, matrix: placed.clone().multiply(new THREE.Matrix4().fromArray(v.matrix)).elements,
          extruder: v.extruder || o.extruder, settings: v.type === 'part' ? { ...o.settings, ...v.settings } : v.settings });
    }
  const base = { geometry, rotX: 0, rotY: 0, scale: [1, 1, 1] as [number, number, number] };
  const source = new File([await bakeStl(base as never, new THREE.Matrix4())], 'Assembly.stl', { type: 'model/stl' });
  const merged: PlateObject = { id: newId(), name: 'Assembly', ...base, extruder: o0.extruder, settings: { ...o0.settings }, source, volumes,
    instances: [{ id: newId(), x: c.x, y: c.y, z: c.z, rotZ: 0 }] };
  undoable('Assemble', () => setState((s) => {
    const at = s.objects.findIndex((o) => o.id === o0.id);
    const rest = s.objects.filter((o) => !ids.includes(o.id));
    return { objects: [...rest.slice(0, at), merged, ...rest.slice(at)], selected: { object: merged.id, instance: merged.instances[0].id }, job: markStale(s.job) };
  }));
}
