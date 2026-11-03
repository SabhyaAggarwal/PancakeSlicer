import * as THREE from 'three';
import { isSelected, type Selected } from '../state/store';

export const SELECTION_COLOR = 0xf2a0b0;
export const HOVER_COLOR = 0xf8cad3;

export function selectedSurface(selection: Selected | null, object: number, instance: number, volume?: number): boolean {
  return isSelected(selection, object, instance) && (selection?.volume === undefined || selection.volume === volume);
}

export class SelectionBounds {
  readonly group = new THREE.Group();
  private readonly geometry: THREE.EdgesGeometry;
  private readonly selected = new THREE.LineBasicMaterial({ color: SELECTION_COLOR, depthTest: false, depthWrite: false, toneMapped: false });
  private readonly hovered = new THREE.LineBasicMaterial({ color: HOVER_COLOR, depthTest: false, depthWrite: false, toneMapped: false });
  private readonly lines = new Map<THREE.Mesh, THREE.LineSegments>();
  private readonly seen = new Set<THREE.Mesh>();
  private readonly center = new THREE.Vector3();
  private readonly size = new THREE.Vector3();

  constructor() {
    const box = new THREE.BoxGeometry(1, 1, 1);
    this.geometry = new THREE.EdgesGeometry(box);
    box.dispose();
  }

  begin() { this.seen.clear(); }

  put(mesh: THREE.Mesh, selected: boolean, hovered: boolean) {
    const box = mesh.geometry.boundingBox;
    if ((!selected && !hovered) || !mesh.visible || !box || box.isEmpty()) return;
    this.seen.add(mesh);
    let line = this.lines.get(mesh);
    if (!line) {
      line = new THREE.LineSegments(this.geometry, this.selected);
      line.matrixAutoUpdate = false;
      line.renderOrder = 8;
      line.raycast = () => {};
      this.lines.set(mesh, line);
      this.group.add(line);
    }
    line.material = selected ? this.selected : this.hovered;
    box.getCenter(this.center); box.getSize(this.size).multiplyScalar(1.006);
    this.size.set(Math.max(this.size.x, 0.1), Math.max(this.size.y, 0.1), Math.max(this.size.z, 0.1));
    line.matrix.makeScale(this.size.x, this.size.y, this.size.z).setPosition(this.center).premultiply(mesh.matrix);
    line.matrixWorldNeedsUpdate = true;
  }

  end() {
    for (const [mesh, line] of this.lines) if (!this.seen.has(mesh)) {
      this.group.remove(line);
      this.lines.delete(mesh);
    }
  }

  dispose() {
    this.group.clear(); this.lines.clear(); this.seen.clear();
    this.geometry.dispose(); this.selected.dispose(); this.hovered.dispose();
  }
}
