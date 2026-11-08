import * as THREE from 'three';
import { isSelected, type Selected } from '../state/store';

export const SELECTION_COLOR = 0xd6d6d6;
export const HOVER_COLOR = 0x6a6a6a;

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
  private readonly boxes = new WeakMap<THREE.Mesh, { key: string; box: THREE.Box3 }>();

  constructor() {
    const box = new THREE.BoxGeometry(1, 1, 1);
    this.geometry = new THREE.EdgesGeometry(box);
    box.dispose();
  }

  recolor(selected: number, hovered: number) { this.selected.color.setHex(selected); this.hovered.color.setHex(hovered); }

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
    const world = this.worldBox(mesh);
    world.getCenter(this.center); world.getSize(this.size).multiplyScalar(1.006);
    this.size.set(Math.max(this.size.x, 0.1), Math.max(this.size.y, 0.1), Math.max(this.size.z, 0.1));
    line.matrix.makeScale(this.size.x, this.size.y, this.size.z).setPosition(this.center);
    line.matrixWorldNeedsUpdate = true;
  }

  private worldBox(mesh: THREE.Mesh): THREE.Box3 {
    const key = `${mesh.geometry.uuid}:${mesh.matrix.elements.join(',')}`, hit = this.boxes.get(mesh);
    if (hit?.key === key) return hit.box;
    const box = new THREE.Box3(), pos = mesh.geometry.getAttribute('position'), v = new THREE.Vector3();
    for (let i = 0; i < pos.count; i++) box.expandByPoint(v.fromBufferAttribute(pos, i).applyMatrix4(mesh.matrix));
    this.boxes.set(mesh, { key, box });
    return box;
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
