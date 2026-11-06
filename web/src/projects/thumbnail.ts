


import * as THREE from 'three';
import { instanceBox } from '../plater/geometry';
import { filamentColor } from '../plater/filamentColor';
import { getState } from '../state/store';

const materials = new Map<string, THREE.MeshStandardMaterial>();
const plain = (color: string) => {
  let m = materials.get(color);
  if (!m) materials.set(color, (m = new THREE.MeshStandardMaterial({ color, roughness: 0.65, metalness: 0.05 })));
  return m;
};

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

  const byId = new Map(s.objects.map((o) => [o.id, o]));
  const hidden: THREE.Object3D[] = [], swapped: [THREE.Mesh, THREE.Material | THREE.Material[]][] = [];
  for (const child of p.scene.children) {
    if ((child as THREE.Light).isLight || !child.visible) continue;
    const { object, volume } = child.userData as { object?: number; volume?: number };
    const o = object === undefined ? undefined : byId.get(object), v = volume === undefined ? undefined : o?.volumes?.find((x) => x.id === volume);
    if (!o || !(child as THREE.Mesh).isMesh || (volume !== undefined && v?.type !== 'part')) { child.visible = false; hidden.push(child); continue; }
    const mesh = child as THREE.Mesh;
    swapped.push([mesh, mesh.material]);
    mesh.material = plain(filamentColor(s, v?.extruder || o.extruder));
  }
  try { p.renderer.render(p.scene, cam); }
  finally {
    for (const child of hidden) child.visible = true;
    for (const [mesh, material] of swapped) mesh.material = material;
  }

  const h = Math.min(c.height, (c.width * HEIGHT) / WIDTH), w = (h * WIDTH) / HEIGHT;
  const t = document.createElement('canvas');
  t.width = WIDTH;
  t.height = HEIGHT;
  t.getContext('2d')!.drawImage(c, (c.width - w) / 2, (c.height - h) / 2, w, h, 0, 0, WIDTH, HEIGHT);
  p.renderer.render(p.scene, p.camera);
  return t.toDataURL('image/jpeg', 0.8);
}
