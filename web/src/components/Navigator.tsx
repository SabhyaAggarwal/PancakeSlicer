import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { cameraCommand, type CameraView } from '../camera';
import { activeCamera } from '../plater/CameraViews';
import { useViewOptions } from '../plater/viewOptions';
import { MenuButton } from './Menu';
import { Glyph } from './Glyph';

const FACES: { view: CameraView; label: string; n: [number, number, number]; u: [number, number, number]; v: [number, number, number] }[] = [
  { view: 'top', label: 'Top', n: [0, 0, 1], u: [1, 0, 0], v: [0, -1, 0] },
  { view: 'bottom', label: 'Bottom', n: [0, 0, -1], u: [1, 0, 0], v: [0, 1, 0] },
  { view: 'front', label: 'Front', n: [0, -1, 0], u: [1, 0, 0], v: [0, 0, -1] },
  { view: 'rear', label: 'Back', n: [0, 1, 0], u: [-1, 0, 0], v: [0, 0, -1] },
  { view: 'left', label: 'Left', n: [-1, 0, 0], u: [0, -1, 0], v: [0, 0, -1] },
  { view: 'right', label: 'Right', n: [1, 0, 0], u: [0, 1, 0], v: [0, 0, -1] },
];
const SIZE = 56;

export function Navigator() {
  const { navigator } = useViewOptions();
  const faces = useRef<(HTMLButtonElement | null)[]>([]);
  const cube=useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!navigator) return;
    let frame = 0, last = '';
    const m = new THREE.Matrix4(), a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), t = new THREE.Vector3();
    const tick = () => {
      frame = requestAnimationFrame(tick);
      const cam = activeCamera();
      if (!cam) {if(cube.current)cube.current.style.visibility='hidden';last='';return;}
      const key = cam.quaternion.toArray().map((x) => x.toFixed(4)).join();
      if (key === last) return;
      last = key;
      m.makeRotationFromQuaternion(cam.quaternion).invert().premultiply(new THREE.Matrix4().makeScale(1, -1, 1));
      FACES.forEach((f, k) => {
        const el = faces.current[k];
        if (!el) return;
        a.fromArray(f.u).applyMatrix4(m);
        b.fromArray(f.v).applyMatrix4(m);
        c.fromArray(f.n).applyMatrix4(m);
        t.copy(c).multiplyScalar(SIZE / 2);
        el.style.transform = `matrix3d(${a.x},${a.y},${a.z},0,${b.x},${b.y},${b.z},0,${c.x},${c.y},${c.z},0,${t.x},${t.y},${t.z},1)`;
      });
      if(cube.current)cube.current.style.visibility='visible';
    };
    tick();
    return () => cancelAnimationFrame(frame);
  }, [navigator]);
  if (!navigator) return null;
  return (
    <><div ref={cube} className="nav3d" style={{visibility:'hidden'}} aria-hidden="true">
      {FACES.map((f, k) => (
        <button key={f.view} type="button" tabIndex={-1} ref={(el) => { faces.current[k] = el; }} className="nav3d-face"
          onClick={() => cameraCommand(f.view)}>{f.label}</button>
      ))}
    </div><MenuButton className="icon-btn canvas-chip nav-view-control" label="Camera view" items={[{label:'Default view',action:()=>cameraCommand('plate')},...FACES.map(face=>({label:`${face.label} view`,action:()=>cameraCommand(face.view)}))]}><Glyph name="view"/></MenuButton></>
  );
}
