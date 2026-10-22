import * as THREE from 'three';
import { instanceMatrix } from './geometry';
import { isPaint, overlayGeometry, paintOf, type Painting } from './paint';
import type { PlateObject, State } from '../state/store';

export class PaintView {
  readonly group = new THREE.Group();
  private readonly overlays = new Map<number, THREE.Mesh>();
  private readonly material = new THREE.MeshBasicMaterial({ vertexColors: true, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -4 });
  private readonly cursor = new THREE.Mesh(new THREE.RingGeometry(0.92, 1, 48), new THREE.MeshBasicMaterial({ color: 0x009688, side: THREE.DoubleSide, depthTest: false, transparent: true }));

  constructor() {
    this.cursor.visible = false;
    this.cursor.renderOrder = 10;
    this.group.add(this.cursor);
  }

  sync(s: State, preview?: Painting | null) {
    const o = isPaint(s.gizmo) ? s.objects.find((ob) => ob.id === s.selected?.object) : undefined;
    const paint = o && isPaint(s.gizmo) ? (preview !== undefined ? preview : paintOf(o, s.gizmo)) : null;
    const seen = new Set<number>();
    if (o && paint) {
      const geometry = overlayGeometry(o.geometry, paint);
      for (const inst of o.instances) {
        seen.add(inst.id);
        let mesh = this.overlays.get(inst.id);
        if (!mesh) {
          mesh = new THREE.Mesh(geometry, this.material);
          mesh.matrixAutoUpdate = false;
          this.overlays.set(inst.id, mesh);
          this.group.add(mesh);
        }
        mesh.geometry = geometry;
        mesh.matrix.copy(instanceMatrix(o, inst));
        mesh.matrixWorldNeedsUpdate = true;
      }
    }
    for (const [id, mesh] of this.overlays)
      if (!seen.has(id)) { this.group.remove(mesh); this.overlays.delete(id); }
  }

  showCursor(point: THREE.Vector3 | null, normal?: THREE.Vector3, radius = 1) {
    this.cursor.visible = !!point;
    if (!point || !normal) return;
    this.cursor.position.copy(point).addScaledVector(normal, 0.05);
    this.cursor.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), normal);
    this.cursor.scale.setScalar(radius);
    this.cursor.updateMatrix();
  }
}

export const meshScale = (o: PlateObject, inst: PlateObject['instances'][number]) => instanceMatrix(o, inst).getMaxScaleOnAxis();
