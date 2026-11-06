

import * as THREE from 'three';
import { instanceMatrix } from '../plater/geometry';
import { getState, subscribeStore } from '../state/store';
import { hoveredIssue, plateIssues, subscribeIssues, type Issue } from './issues';

const css = (v: string, d: string) => getComputedStyle(document.documentElement).getPropertyValue(v).trim() || d;

export function mountMarks(scene: THREE.Scene, render: () => void): () => void {
  const root = new THREE.Group();
  root.renderOrder = 3;
  scene.add(root);

  const tint = (opacity: number) => new THREE.MeshBasicMaterial({ color: css('--danger', '#c62828'), transparent: true, opacity, depthWrite: false, depthTest: false,
    polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4, side: THREE.DoubleSide });
  const line = (opacity: number) => new THREE.LineBasicMaterial({ color: css('--danger', '#c62828'), transparent: true, opacity });
  const mats = { face: tint(0.5), faceHot: tint(0.85), edge: line(0.6), edgeHot: line(1) };
  const geos = new Map<string, THREE.BufferGeometry>();

  const facets = (i: Issue) => {
    const k = `f:${i.key}:${i.tris.length}`;
    let g = geos.get(k);
    if (!g) {
      const o = getState().objects.find((x) => x.id === i.objectId)!;
      const pos = o.geometry.attributes.position, idx = o.geometry.index;
      const out = new Float32Array(i.tris.length * 9);
      i.tris.forEach((t, n) => {
        for (let c = 0; c < 3; c++) {
          const v = idx ? idx.getX(3 * t + c) : 3 * t + c;
          out.set([pos.getX(v), pos.getY(v), pos.getZ(v)], 9 * n + 3 * c);
        }
      });
      geos.set(k, (g = new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(out, 3))));
    }
    return g;
  };
  const outline = (i: Issue) => {
    const o = getState().objects.find((x) => x.id === i.objectId)!;
    const k = `e:${o.geometry.uuid}`;
    let g = geos.get(k);
    if (!g) geos.set(k, (g = new THREE.EdgesGeometry(o.geometry, 30)));
    return g;
  };

  const sync = () => {
    root.clear();
    const s = getState(), hot = hoveredIssue(), seen = new Set<string>();
    if (s.tab === 'prepare')
      for (const i of plateIssues()) {
        const o = s.objects.find((x) => x.id === i.objectId);
        if (!o) continue;
        const hit = hot === i.key;
        const g = i.tris.length ? facets(i) : outline(i);
        seen.add([...geos].find(([, v]) => v === g)![0]);
        for (const inst of o.instances) {
          const m = i.tris.length ? new THREE.Mesh(g, hit ? mats.faceHot : mats.face) : new THREE.LineSegments(g, hit ? mats.edgeHot : mats.edge);
          m.matrixAutoUpdate = false;
          m.matrix.copy(instanceMatrix(o, inst));
          root.add(m);
        }
      }
    for (const [k, g] of geos) if (!seen.has(k)) { g.dispose(); geos.delete(k); }
    render();
  };
  let last: unknown[] = [];
  const offStore = subscribeStore(() => {
    const s = getState(), now = [s.objects, s.tab];
    if (now.every((v, i) => v === last[i])) return;
    last = now;
    sync();
  });
  const offIssues = subscribeIssues(sync);
  sync();
  return () => {
    offStore();
    offIssues();
    scene.remove(root);
    geos.forEach((g) => g.dispose());
    Object.values(mats).forEach((m) => m.dispose());
  };
}
