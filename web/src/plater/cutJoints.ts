import * as THREE from 'three';
import type { CutConnector, CutConnectorAttribs, CutGroove, CutPlane, State } from '../state/store';
import { cutNormal, cutPoint } from './cut';

export const CONNECTOR: CutConnectorAttribs = { type: 'plug', style: 'prism', shape: 'circle', depth: 3, depthTolerance: 0.1, size: 2.5, sizeTolerance: 0,
  angle: 0, bulge: 0.15, space: 0.3 };
export const GROOVE: CutGroove = { depth: 2, depthTolerance: 0.1, width: 8, widthTolerance: 0.1, flapsAngle: 60, angle: 0, count: 1, gap: 2 };

const Z = new THREE.Vector3(0, 0, 1);
export const planeRotation = (n: THREE.Vector3) => new THREE.Quaternion().setFromUnitVectors(Z, n.clone().normalize());

export function connectorWorld(s: State, c: CutPlane, [u, v]: [number, number]): THREE.Vector3 {
  return new THREE.Vector3(u, v, 0).applyQuaternion(planeRotation(cutNormal(c))).add(cutPoint(s, c)!);
}

export function planeUV(s: State, c: CutPlane, world: THREE.Vector3): [number, number] {
  const local = world.clone().sub(cutPoint(s, c)!).applyQuaternion(planeRotation(cutNormal(c)).invert());
  return [local.x, local.y];
}

export const SECTORS = { triangle: 3, square: 4, hexagon: 6, circle: 48 } as const;

export function jointFields(s: State, c: CutPlane, toStl: THREE.Matrix4, p: THREE.Vector3, n: THREE.Vector3): Record<string, string> {
  if (c.mode === 'dovetail') {
    const g = { ...GROOVE, ...c.groove };
    return { cut_dovetail: JSON.stringify({ depth: g.depth, width: g.width, flaps_angle: g.flapsAngle, angle: g.angle,
      depth_tolerance: g.depthTolerance, width_tolerance: g.widthTolerance, count: g.count, gap: g.gap }) };
  }
  if (!c.connectors?.length || c.toParts) return {};
  const toTool = planeRotation(n).invert();
  const ax = new THREE.Vector3(1, 0, 0).applyQuaternion(planeRotation(cutNormal(c))).transformDirection(toStl).applyQuaternion(toTool);
  const turn = Math.atan2(ax.y, ax.x) * 180 / Math.PI;
  return {
    cut_connectors: JSON.stringify(c.connectors.map(({ pos, a }) => {
      const q = connectorWorld(s, c, pos).applyMatrix4(toStl).sub(p).applyQuaternion(toTool);
      return {
        type: a.type, style: a.type === 'plug' ? a.style : 'prism', shape: a.type === 'snap' ? 'circle' : a.shape,
        pos: [+q.x.toFixed(4), +q.y.toFixed(4)], radius: a.size / 2, height: a.depth,
        radius_tolerance: a.sizeTolerance / 2, height_tolerance: a.depthTolerance, angle: +(a.angle - turn).toFixed(4),
        ...(a.type === 'snap' ? { snap_bulge: a.bulge, snap_space: a.space } : {}),
      };
    })),
  };
}

export function flipped(s: State, c: CutPlane): Partial<CutPlane> {
  const next: CutPlane = { ...c, rotX: ((c.rotX + 360) % 360) - 180 };
  const q = planeRotation(cutNormal(c)), back = planeRotation(cutNormal(next)).invert();
  return {
    rotX: next.rotX,
    connectors: c.connectors?.map((k) => {
      const d = new THREE.Vector3(1, 0, 0).applyAxisAngle(new THREE.Vector3(0, 0, 1), -k.a.angle * Math.PI / 180).applyQuaternion(q).applyQuaternion(back);
      return { pos: planeUV(s, next, connectorWorld(s, c, k.pos)), a: { ...k.a, angle: (((-Math.atan2(d.y, d.x) * 180) / Math.PI) % 360 + 360) % 360 } };
    }),
  };
}

export function connectorAt(list: CutConnector[], [u, v]: [number, number]): number {
  let best = -1, min = Infinity;
  list.forEach((k, i) => {
    const d = Math.hypot(k.pos[0] - u, k.pos[1] - v);
    if (d <= Math.max(1.5, k.a.size / 2) && d < min) [best, min] = [i, d];
  });
  return best;
}
