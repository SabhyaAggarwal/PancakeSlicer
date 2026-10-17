import * as THREE from 'three';
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
import type { SystemPreset } from '../api/types';
import { bedBounds, parsePoints } from './geometry';

const stlCache = new Map<string, Promise<THREE.BufferGeometry | null>>();
const texCache = new Map<string, Promise<THREE.Texture | null>>();

function loadStl(url: string) {
  if (!stlCache.has(url))
    stlCache.set(
      url,
      fetch(url)
        .then((r) => (r.ok ? r.arrayBuffer() : Promise.reject()))
        .then((b) => {
          const g = new STLLoader().parse(b);
          g.computeVertexNormals();
          return g;
        })
        .catch(() => null),
    );
  return stlCache.get(url)!;
}

function loadSvgTexture(url: string) {
  if (!texCache.has(url))
    texCache.set(
      url,
      new Promise((resolve) => {
        const img = new Image();
        img.onload = () => {
          const k = 2048 / Math.max(img.width, img.height);
          const c = document.createElement('canvas');
          c.width = Math.round(img.width * k);
          c.height = Math.round(img.height * k);
          c.getContext('2d')!.drawImage(img, 0, 0, c.width, c.height);
          const t = new THREE.CanvasTexture(c);
          t.colorSpace = THREE.SRGBColorSpace;
          t.anisotropy = 8;
          resolve(t);
        };
        img.onerror = () => resolve(null);
        img.src = url;
      }),
    );
  return texCache.get(url)!;
}

export function buildBed(machine: SystemPreset | undefined, onReady: () => void): THREE.Group {
  const group = new THREE.Group();
  group.name = 'bed';
  const area = parsePoints(machine?.config.printable_area);
  const pts = area.length >= 3 ? area : [new THREE.Vector2(0, 0), new THREE.Vector2(200, 0), new THREE.Vector2(200, 200), new THREE.Vector2(0, 200)];
  const { min, max } = bedBounds(pts);
  const shape = new THREE.Shape(pts);

  const plateGeo = new THREE.ExtrudeGeometry(shape, { depth: 1, bevelEnabled: false });
  plateGeo.translate(0, 0, -1.02);
  const plate = new THREE.Mesh(plateGeo, new THREE.MeshLambertMaterial({ color: 0xb9bcbd }));
  plate.receiveShadow = false;
  group.add(plate);

  const texMat = new THREE.MeshBasicMaterial({ color: 0xdcdcdc, transparent: true, depthWrite: false });
  const surfGeo = new THREE.ShapeGeometry(shape);
  const sp = surfGeo.attributes.position, uv = surfGeo.attributes.uv;
  for (let i = 0; i < sp.count; i++) uv.setXY(i, (sp.getX(i) - min.x) / (max.x - min.x), (sp.getY(i) - min.y) / (max.y - min.y));
  const surf = new THREE.Mesh<THREE.BufferGeometry, THREE.MeshBasicMaterial>(surfGeo, texMat);
  surf.position.z = 0.01;
  group.add(surf);

  const grid: number[] = [];
  for (let x = Math.ceil(min.x / 10) * 10; x <= max.x; x += 10) grid.push(x, min.y, 0.02, x, max.y, 0.02);
  for (let y = Math.ceil(min.y / 10) * 10; y <= max.y; y += 10) grid.push(min.x, y, 0.02, max.x, y, 0.02);
  const gridGeo = new THREE.BufferGeometry();
  gridGeo.setAttribute('position', new THREE.Float32BufferAttribute(grid, 3));
  group.add(new THREE.LineSegments(gridGeo, new THREE.LineBasicMaterial({ color: 0x8f9293, transparent: true, opacity: 0.45 })));

  const outline = new THREE.LineLoop(
    new THREE.BufferGeometry().setFromPoints(pts.map((p) => new THREE.Vector3(p.x, p.y, 0.03))),
    new THREE.LineBasicMaterial({ color: 0x5f6263 }),
  );
  group.add(outline);

  const vendor = machine?.vendor ?? '';
  if (machine?.bed_model) {
    void loadStl(`/profiles/${vendor}/${machine.bed_model}`).then((g) => {
      if (!g) return;
      g.computeBoundingBox();
      const bb = g.boundingBox!;
      const model = new THREE.Mesh(g, new THREE.MeshLambertMaterial({ color: 0x3c3f40 }));
      const bbl = machine.bed_model!.startsWith('bbl-3dp-');
      const c = bedBounds(pts).center;
      model.position.set(bbl ? 0 : c.x, bbl ? 0 : c.y, -bb.max.z - 0.03);
      group.add(model);
      plate.visible = false;
      if (bbl) {
        const w = bb.max.x - bb.min.x, h = bb.max.y - bb.min.y;
        const tg = new THREE.PlaneGeometry(w, h);
        tg.translate(bb.min.x + w / 2, bb.min.y + h / 2, 0.005);
        surf.geometry.dispose();
        surf.geometry = tg;
      }
      onReady();
    });
  }
  if (machine?.bed_texture) {
    void loadSvgTexture(`/profiles/${vendor}/${machine.bed_texture}`).then((t) => {
      if (!t) return;
      texMat.map = t;
      texMat.color.set(0xffffff);
      texMat.needsUpdate = true;
      onReady();
    });
  }
  return group;
}
