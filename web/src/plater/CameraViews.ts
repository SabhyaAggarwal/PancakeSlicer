import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { autoPerspective, cameraProjection, perspective, VIEW_DIRECTIONS, type CameraCommand, type CameraView } from '../camera';
import { getState, subscribeStore } from '../state/store';
import { bedBounds } from './geometry';
import { ownsKeys } from '../keys';

const registry = new Set<CameraViews>();
export const activeCamera = () => [...registry].find((v) => v.isActive())?.camera ?? null;

export class CameraViews {
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
  readonly controls: OrbitControls;
  private area: THREE.Vector2[] = [];
  private width = 1;
  private height = 1;
  private axisView = false;
  private unsubscribe: () => void;

  constructor(host: HTMLElement, private active: () => boolean, private changed: () => void) {
    this.camera = new THREE.PerspectiveCamera(35, 1, 0.5, 5000);
    this.camera.up.set(0, 0, 1);
    this.controls = new OrbitControls(this.camera, host);
    this.controls.mouseButtons = { LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.PAN };
    this.controls.zoomToCursor = true;
    this.controls.addEventListener('change', this.onOrbit);
    registry.add(this);
    window.addEventListener('camera-command', this.onCommand);
    window.addEventListener('keydown', this.onKey);
    let wasActive = this.active();
    this.unsubscribe = subscribeStore(() => {
      const active = this.active();
      if (active && !wasActive) cameraProjection(this.camera instanceof THREE.PerspectiveCamera);
      wasActive = active;
    });
  }

  private onOrbit = () => {
    if (this.axisView && autoPerspective) {
      const d = this.camera.position.clone().sub(this.controls.target).normalize();
      if (Math.max(Math.abs(d.x), Math.abs(d.y), Math.abs(d.z)) < 0.99999) {
        this.axisView = false;
        this.projection(true);
      }
    }
    this.changed();
  };
  private onCommand = (event: Event) => {
    const command = (event as CustomEvent<CameraCommand>).detail;
    if (command === 'auto') return;
    if (command === 'perspective' || command === 'orthogonal') this.projection(perspective);
    else if (this.active()) this.view(command);
  };
  private onKey = (event: KeyboardEvent) => {
    if (!this.active() || event.altKey || event.shiftKey || ownsKeys(event) ||
        getState().dialog || getState().flushDialog || getState().appDialog || document.querySelector('dialog[open]')) return;
    if (!event.ctrlKey && !event.metaKey && (event.key === 'i' || event.key === 'o')) {
      event.preventDefault();
      this.zoom(event.key === 'i' ? 1.25 : 0.8);
      return;
    }
    const view = VIEW_DIRECTIONS[Number(event.key)];
    if (!/^[0-6]$/.test(event.key) || !view || getState().gizmo === 'color' && event.key !== '0') return;
    event.preventDefault();
    this.view(view);
  };

  setBed(area: THREE.Vector2[]) { this.area = area; this.view('plate'); }

  zoom(factor: number) {
    if (this.camera instanceof THREE.OrthographicCamera) {
      this.camera.zoom = Math.min(50, Math.max(0.05, this.camera.zoom * factor));
      this.camera.updateProjectionMatrix();
    } else {
      const offset = this.camera.position.clone().sub(this.controls.target);
      this.camera.position.copy(this.controls.target).add(offset.multiplyScalar(1 / factor));
    }
    this.controls.update();
    this.changed();
  }

  view(view: CameraView) {
    const { min, max, center } = bedBounds(this.area);
    const size = Math.max(max.x - min.x, max.y - min.y, 1);
    const distance = view === 'plate' ? size * 2.2 : Math.max(this.camera.position.distanceTo(this.controls.target), size * 1.5);
    if (view === 'plate') this.controls.target.set(center.x, center.y, 0);
    const offsets = { plate: [0, -Math.SQRT1_2, Math.SQRT1_2], top: [0, 0, 1], bottom: [0, 0, -1],
      front: [0, -1, 0], rear: [0, 1, 0], left: [-1, 0, 0], right: [1, 0, 0] };
    this.axisView = false;
    this.camera.up.set(0, view === 'top' ? 1 : view === 'bottom' ? -1 : 0, view === 'top' || view === 'bottom' ? 0 : 1);
    if (view === 'plate') this.camera.up.set(0, Math.SQRT1_2, Math.SQRT1_2);
    this.camera.position.copy(this.controls.target).add(new THREE.Vector3(...offsets[view] as [number, number, number]).multiplyScalar(distance));
    this.controls.object = this.camera;
    this.controls.update();
    this.axisView = view !== 'plate';
    this.projection(autoPerspective ? view === 'plate' : perspective);
    this.changed();
  }

  projection(usePerspective: boolean) {
    const old = this.camera;
    if ((old instanceof THREE.PerspectiveCamera) === usePerspective) return;
    const halfHeight = old instanceof THREE.OrthographicCamera ? old.top / old.zoom :
      old.position.distanceTo(this.controls.target) * Math.tan(THREE.MathUtils.degToRad(old.fov / 2)) / old.zoom;
    const camera = usePerspective ? new THREE.PerspectiveCamera(35, this.width / this.height, 0.5, 5000) :
      new THREE.OrthographicCamera(-halfHeight * this.width / this.height, halfHeight * this.width / this.height, halfHeight, -halfHeight, 0.5, 5000);
    camera.position.copy(old.position);
    camera.quaternion.copy(old.quaternion);
    camera.up.copy(old.up);
    if (usePerspective) {
      const distance = halfHeight / Math.tan(THREE.MathUtils.degToRad(17.5));
      camera.position.copy(old.position).sub(this.controls.target).setLength(distance).add(this.controls.target);
    }
    this.camera = camera;
    this.controls.object = camera;
    this.controls.update();
    if (this.active()) cameraProjection(usePerspective);
    this.changed();
  }

  resize(width: number, height: number) {
    this.width = width; this.height = Math.max(1, height);
    if (this.camera instanceof THREE.PerspectiveCamera) this.camera.aspect = width / this.height;
    else {
      this.camera.right = this.camera.top * width / this.height;
      this.camera.left = -this.camera.right;
    }
    this.camera.updateProjectionMatrix();
  }
  isActive() { return this.active(); }
  dispose() {
    registry.delete(this);
    window.removeEventListener('camera-command', this.onCommand);
    window.removeEventListener('keydown', this.onKey);
    this.unsubscribe();
    this.controls.dispose();
  }
}
