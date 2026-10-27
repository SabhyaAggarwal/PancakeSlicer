import { useState } from 'react';
import * as THREE from 'three';
import { setState, updateVolume, useStore, type Instance, type PlateObject, type Volume } from '../state/store';
import { instanceMatrix, volumeBox } from '../plater/geometry';
import { Num } from './Num';

let partCoordinates: 'world' | 'object' = 'world';

export default function PartManipulation({ o, inst, v, docked }: { o: PlateObject; inst: Instance; v: Volume; docked: boolean }) {
  const gizmo = useStore((s) => s.gizmo);
  const uniform = useStore((s) => s.uniformScale);
  const [coordinates, setCoordinates] = useState(partCoordinates);
  const local = coordinates === 'object' && gizmo === 'move' && !docked;
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
  return (
    <div className={`manip-panel${docked ? ' docked' : ''}`}>
      <div className="manip-title">{v.name}</div>
      {gizmo === 'move' && !docked && (
        <label className="manip-coords" title="Coordinate system used for transform actions.">
          <select className="combo" value={coordinates} onChange={(e) => { partCoordinates = e.target.value as 'world' | 'object'; setCoordinates(partCoordinates); }}>
            <option value="world">World</option>
            <option value="object">Object</option>
          </select>
        </label>
      )}
      <div className="manip-grid">
        <span />
        <span className="axis x">X</span>
        <span className="axis y">Y</span>
        <span className="axis z">Z</span>
        <span>Position</span>
        {[0, 1, 2].map((a) => <Num key={a} value={(local ? inObject : centre).getComponent(a)} onCommit={(x) => move(a, x)} />)}
        <span>Size mm</span>
        {[0, 1, 2].map((a) => <Num key={a} value={size.getComponent(a)} onCommit={(x) => resize(a, x)} />)}
        {(gizmo === 'scale' || docked) && (
          <label className="manip-lock">
            <input type="checkbox" checked={uniform} onChange={(e) => setState({ uniformScale: e.target.checked })} />
            Uniform scale
          </label>
        )}
      </div>
    </div>
  );
}
