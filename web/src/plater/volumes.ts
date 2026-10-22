import * as THREE from 'three';
import { bedArea } from '../actions';
import { addVolume, getState, newId, toast, type Volume, type VolumeType } from '../state/store';
import { bedBounds, instanceBox, instanceMatrix, loadMesh, primitiveGeometry, volumeGeometry } from './geometry';

export const TYPE_LABEL: Record<VolumeType, string> = {
  part: 'Part', negative: 'Negative Part', modifier: 'Modifier', support_blocker: 'Support Blocker', support_enforcer: 'Support Enforcer',
};

export type Shape = 'Cube' | 'Cylinder' | 'Sphere' | 'Cone' | 'Disc' | 'Torus';
export const SHAPES: Shape[] = ['Cube', 'Cylinder', 'Sphere', 'Cone', 'Disc', 'Torus'];

export const VOLUME_MENU: [VolumeType, string, string][] = [
  ['part', 'Add Part', 'menu_add_part'],
  ['negative', 'Add Negative Part', 'menu_add_negative'],
  ['modifier', 'Add Modifier', 'menu_add_modifier'],
  ['support_blocker', 'Add Support Blocker', 'menu_support_blocker'],
  ['support_enforcer', 'Add Support Enforcer', 'menu_support_enforcer'],
];
export const volumeIcon = (type: VolumeType) => VOLUME_MENU.find(([t]) => t === type)![2];

function add(objectId: number, type: VolumeType, name: string, geometry: THREE.BufferGeometry, world: (box: THREE.Box3, size: THREE.Vector3) => THREE.Vector3, step: string) {
  const s = getState();
  const o = s.objects.find((ob) => ob.id === objectId);
  if (!o) return;
  const inst = o.instances.find((i) => i.id === s.selected?.instance) ?? o.instances[0];
  const size = geometry.boundingBox!.getSize(new THREE.Vector3());
  const at = world(instanceBox(o, inst), size);
  const matrix = instanceMatrix(o, inst).invert().multiply(new THREE.Matrix4().makeTranslation(at.x, at.y, at.z)).elements;
  const volume: Volume = { id: newId(), name, type, geometry, matrix, extruder: 0, settings: {} };
  addVolume(o.id, volume, step);
}

export function addPrimitive(objectId: number, type: VolumeType, shape: Shape) {
  const { min, max } = bedBounds(bedArea());
  const geometry = primitiveGeometry(shape, 0.1 * Math.max(max.x - min.x, max.y - min.y));
  add(objectId, type, `Generic-${shape}`, geometry,
    (b, size) => new THREE.Vector3(b.max.x + size.x / 2, b.min.y + size.y / 2, b.min.z + size.z / 2), 'Add primitive');
}

export function loadVolume(objectId: number, type: VolumeType) {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.stl,.obj';
  input.onchange = async () => {
    const file = input.files?.[0];
    if (!file) return;
    try {
      const loaded = await loadMesh(file);
      const geometry = volumeGeometry(loaded.attributes.position.array as Float32Array, loaded.index?.array as Uint32Array,
        loaded.attributes.normal?.array as Float32Array | undefined);
      add(objectId, type, file.name.replace(/\.(stl|obj)$/i, ''), geometry,
        (b, size) => new THREE.Vector3((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, b.min.z + size.z / 2),
        type === 'part' ? 'Load Part' : 'Load Modifier');
    } catch (e) {
      toast(e instanceof Error ? e.message : String(e));
    }
  };
  input.click();
}
