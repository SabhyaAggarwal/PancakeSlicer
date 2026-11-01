import * as THREE from 'three';
import { updateVolume, useStore, type Instance, type PlateObject, type Volume } from '../state/store';
import { instanceMatrix, volumeBox } from '../plater/geometry';
import { Row } from './Transform';

export default function PartManipulation({ o, inst, v, local, lock }: { o: PlateObject; inst: Instance; v: Volume; local: boolean; lock: React.ReactNode }) {
  const uniform = useStore((s) => s.uniformScale);
  const box = volumeBox(o, inst, v);
  const centre = box.getCenter(new THREE.Vector3()), size = box.getSize(new THREE.Vector3());
  const toWorld = instanceMatrix(o, inst), toObject = toWorld.clone().invert();
  const inObject = centre.clone().applyMatrix4(toObject);
  const change = (m: THREE.Matrix4, name: string) => updateVolume(o.id, v.id,
    { matrix: toObject.clone().multiply(m).multiply(toWorld).multiply(new THREE.Matrix4().fromArray(v.matrix)).elements }, name);
  const move = (axis: number, value: number) => {
    const target = local ? inObject.clone().setComponent(axis, value).applyMatrix4(toWorld) : centre.clone().setComponent(axis, value);
    change(new THREE.Matrix4().makeTranslation(target.x - centre.x, target.y - centre.y, target.z - centre.z), 'Move');
  };
  const resize = (axis: number, value: number) => {
    const f = [0, 1, 2].map((k) => (uniform || k === axis ? Math.max(0.01, value) / Math.max(1e-6, size.getComponent(axis)) : 1));
    change(new THREE.Matrix4().makeTranslation(centre.x, centre.y, centre.z).multiply(new THREE.Matrix4().makeScale(f[0], f[1], f[2]))
      .multiply(new THREE.Matrix4().makeTranslation(-centre.x, -centre.y, -centre.z)), 'Scale');
  };
  const none = () => undefined;
  return (
    <>
      <Row label="Position" names="Position" values={(local ? inObject : centre).toArray()} onCommit={move} />
      <Row label="Rotation" names="Rotation" onCommit={none} />
      <Row label="Scale %" names="Scale" onCommit={none} action={lock} />
      <Row label="Size mm" names="Size" values={size.toArray()} onCommit={resize} />
    </>
  );
}
