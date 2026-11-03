import * as THREE from 'three';
import type { PlateObject } from '../state/store';
import type { Config } from '../api/types';
import { getState, setState } from '../state/store';
import { prepareProjectProfileImport } from '../profiles/project-import';
import { readMixes } from './mixed';

export function adoptProject(config:Config,slots:number,plateKeys:Set<string>) {
  const mixed=[config.filament_is_mixed].flat().some(v=>['1','true'].includes(String(v)));
  setState({...prepareProjectProfileImport(getState(),config,slots,plateKeys),...(mixed?{mixes:readMixes(config)}:{})});
}

export function centreGeometryOnly(bed: {x:number;y:number}[]) {
  const box = new THREE.Box3();
  for (const o of getState().objects) {
    o.geometry.computeBoundingBox();
    for (const i of o.instances) box.union(o.geometry.boundingBox!.clone().translate(new THREE.Vector3(i.x, i.y, i.z)));
  }
  const xs = bed.map((p) => p.x), ys = bed.map((p) => p.y);
  if (box.isEmpty() || !bed.length) return;
  const dx = (Math.min(...xs) + Math.max(...xs) - box.min.x - box.max.x) / 2, dy = (Math.min(...ys) + Math.max(...ys) - box.min.y - box.max.y) / 2;
  const shift = (o: PlateObject): PlateObject => {
    const p = o.project!, [px, py] = p.plateOrigin ?? [0, 0];
    return { ...o, instances: o.instances.map((i) => ({ ...i, x: i.x + dx, y: i.y + dy })),
      project: { ...p, plateOrigin: [px - dx, py - dy], ...(p.origin ? { origin: [p.origin[0] + dx, p.origin[1] + dy, p.origin[2]] as [number, number, number] } : {}) } };
  };
  setState((st) => ({ objects: st.objects.map(shift), projectPlates: st.projectPlates.map((p) => ({ ...p, objects: p.objects.map(shift) })) }));
}
