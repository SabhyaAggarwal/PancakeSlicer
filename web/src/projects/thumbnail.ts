import * as THREE from 'three';
import { instanceBox } from '../plater/geometry';
import { getState } from '../state/store';

const WIDTH = 320, HEIGHT = 240;

export function projectThumbnail(): string | null {
  const p = (window as unknown as { __plater?: { renderer: THREE.WebGLRenderer; scene: THREE.Scene; camera: THREE.Camera } }).__plater;
  const c = p?.renderer.domElement, s = getState();
  if (!p || !c?.clientWidth || !c.clientHeight || s.tab !== 'prepare' || !s.objects.length) return null;
  const box = new THREE.Box3();
  for (const o of s.objects) for (const i of o.instances) box.union(instanceBox(o, i));
  const sphere = box.getBoundingSphere(new THREE.Sphere());
  const aspect = c.width / c.height;
  const cam = new THREE.PerspectiveCamera(30, aspect, 1, 10000);
  const dir = new THREE.Vector3(0.45, -1, 0.8).normalize();
  const fit = Math.min(1, aspect * (HEIGHT / WIDTH));
  const dist = (1.15 * sphere.radius) / Math.sin(THREE.MathUtils.degToRad(15)) / fit;
  cam.position.copy(sphere.center).addScaledVector(dir, dist);
  cam.up.set(0, 0, 1);
  cam.lookAt(sphere.center);
  cam.near = Math.max(0.1, dist - 4 * sphere.radius);
  cam.far = dist + 4 * sphere.radius;
  cam.updateProjectionMatrix();
  p.renderer.render(p.scene, cam);
  const h = Math.min(c.height, (c.width * HEIGHT) / WIDTH), w = (h * WIDTH) / HEIGHT;
  const t = document.createElement('canvas');
  t.width = WIDTH;
  t.height = HEIGHT;
  t.getContext('2d')!.drawImage(c, (c.width - w) / 2, (c.height - h) / 2, w, h, 0, 0, WIDTH, HEIGHT);
  p.renderer.render(p.scene, p.camera);
  return t.toDataURL('image/jpeg', 0.8);
}
