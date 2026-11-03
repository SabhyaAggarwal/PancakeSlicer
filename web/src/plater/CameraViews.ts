import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { autoPerspective, cameraPrefs, cameraProjection, perspective, subscribeCamera, VIEW_DIRECTIONS, type CameraCommand, type CameraView } from '../camera';
import { getState, subscribeStore } from '../state/store';
import { bedBounds } from './geometry';
import { ownsKeys } from '../keys';

const registry = new Set<CameraViews>();
export const activeCamera = () => [...registry].find((v) => v.isActive())?.camera ?? null;
let lastActive: CameraViews | null = null;

export class CameraViews {
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
  readonly controls: OrbitControls;
  private area: THREE.Vector2[] = [];
  private width = 1;
  private height = 1;
  private insets = { left: 0, right: 0 };
  private axisView = false;
  private axisDirection = new THREE.Vector3();
  private handedOff = false;
  private focusRadius = 1;
  private bounds = new THREE.Box3();
  private adjusting = false;
  private unsubscribe: () => void;
  private unsubscribePrefs: () => void;

  constructor(private host: HTMLElement, private active: () => boolean, private changed: () => void) {
    this.camera = new THREE.PerspectiveCamera(35, 1, 0.5, 5000);
    this.camera.up.set(0, 0, 1);
    this.controls = new OrbitControls(this.camera, host);
    this.controls.mouseButtons = { LEFT: THREE.MOUSE.ROTATE, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.PAN };
    this.controls.minDistance = 2;
    this.controls.minPolarAngle = THREE.MathUtils.degToRad(1);
    this.controls.maxPolarAngle = Math.PI - this.controls.minPolarAngle;
    this.applyPrefs();
    host.addEventListener('wheel', this.onWheel, { capture: true, passive: false });
    this.unsubscribePrefs = subscribeCamera(this.applyPrefs);
    host.addEventListener('pointermove', this.onTouchpadMove);
    this.controls.addEventListener('change', this.onOrbit);
    registry.add(this);
    window.addEventListener('camera-command', this.onCommand);
    window.addEventListener('keydown', this.onKey);
    let wasActive = this.active();
    if (wasActive && lastActive) this.takePose(lastActive);
    if (wasActive) lastActive = this;
    this.unsubscribe = subscribeStore(() => {
      const active = this.active();
      if (active && !wasActive) {
        if (lastActive && lastActive !== this) this.takePose(lastActive);
        cameraProjection(this.camera instanceof THREE.PerspectiveCamera);
      }
      if (active) lastActive = this;
      wasActive = active;
    });
  }

  private applyPrefs = () => {
    this.controls.zoomToCursor = cameraPrefs.zoomToMouse;
    this.controls.rotateSpeed = cameraPrefs.orbitMult;
    this.controls.zoomSpeed = cameraPrefs.reverseZoom ? -1 : 1;
  };
  private onTouchpadMove = (event: PointerEvent) => {
    if (cameraPrefs.style !== 'touchpad' || !this.controls.enabled || event.pointerType === 'touch' ||
        event.buttons || !this.active() || !(event.altKey || event.shiftKey)) return;
    this.controls.dispatchEvent({ type: 'start' });
    const h = this.host.clientHeight || 1;
    if (event.altKey) {
      this.controls.rotateLeft(2 * Math.PI * event.movementX / h * cameraPrefs.orbitMult);
      this.controls.rotateUp(2 * Math.PI * event.movementY / h * cameraPrefs.orbitMult);
    } else this.controls.pan(event.movementX, event.movementY);
    this.controls.dispatchEvent({ type: 'end' });
  };

  private wheelTrackpad = false;
  private wheelAt = -Infinity;
  private onWheel = (e: WheelEvent) => {
    if (!this.active() || !this.controls.enabled || !this.controls.enablePan || e.ctrlKey || e.metaKey) return;
    if (e.timeStamp - this.wheelAt > 250) {
      const legacy = (e as WheelEvent & { wheelDeltaY?: number }).wheelDeltaY;
      this.wheelTrackpad = cameraPrefs.style === 'touchpad' || e.deltaX !== 0 ||
        e.deltaMode === 0 && (legacy ? legacy === -3 * e.deltaY : Math.abs(e.deltaY) < 50);
    }
    this.wheelAt = e.timeStamp;
    if (!this.wheelTrackpad) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    const k = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 100 : 1;
    this.controls.dispatchEvent({ type: 'start' });
    this.controls.pan(-e.deltaX * k, -e.deltaY * k);
    this.controls.dispatchEvent({ type: 'end' });
  };

  private focus(center: THREE.Vector3, radius: number) {
    this.focusRadius = radius;
    this.controls.cursor.copy(center);
    this.controls.maxTargetRadius = radius;
    this.controls.maxDistance = radius * 12;
    this.zoomLimits();
  }

  private zoomLimits() {
    if (!(this.camera instanceof THREE.OrthographicCamera)) return;
    this.controls.minZoom = this.camera.top / (this.focusRadius * 12);
    this.controls.maxZoom = this.camera.top / 0.25;
    this.camera.zoom = THREE.MathUtils.clamp(this.camera.zoom, this.controls.minZoom, this.controls.maxZoom);
    this.camera.updateProjectionMatrix();
  }

  setBounds(box: THREE.Box3) {
    if (this.bounds.equals(box)) return;
    this.bounds.copy(box);
    this.constrainZoom();
  }

  private constrainZoom() {
    if (this.bounds.isEmpty()) { this.controls.minDistance = 2; return; }
    const offset = this.camera.position.clone().sub(this.controls.target), distance = offset.length();
    if (!distance) return;
    const dir = offset.divideScalar(distance), { min, max } = this.bounds;
    const front = new THREE.Vector3(dir.x >= 0 ? max.x : min.x, dir.y >= 0 ? max.y : min.y, dir.z >= 0 ? max.z : min.z);
    const limit = Math.max(2, front.sub(this.controls.target).dot(dir) + 0.3 + this.camera.near);
    this.controls.minDistance = limit;
    this.controls.maxDistance = Math.max(this.controls.maxDistance, limit * 2);
    if (this.camera instanceof THREE.PerspectiveCamera && distance < limit) {
      this.camera.position.copy(this.controls.target).addScaledVector(dir, limit);
      this.adjusting = true;
      try { this.controls.update(); } finally { this.adjusting = false; }
    }
  }

  private clip() {
    const c = this.camera;
    const d = c.position.distanceTo(this.controls.target);
    const near = THREE.MathUtils.clamp(d / 200, 0.05, 5), far = Math.max(5000, d + this.focusRadius * 4);
    if (near === c.near && far === c.far) return;
    c.near = near;
    c.far = far;
    c.updateProjectionMatrix();
  }

  private onOrbit = () => {
    if (this.adjusting) return;
    this.constrainZoom();
    this.clip();
    if (this.axisView && autoPerspective) {
      const d = this.camera.position.clone().sub(this.controls.target).normalize();
      if (d.dot(this.axisDirection) < 0.99999) {
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
    if (!this.active() || event.defaultPrevented || event.ctrlKey || event.metaKey || event.altKey || event.shiftKey || ownsKeys(event) ||
        getState().dialog || getState().flushDialog || getState().appDialog || document.querySelector('dialog[open]')) return;
    if (event.key === 'i' || event.key === 'o') {
      event.preventDefault();
      this.zoom(event.key === 'i' ? 1.25 : 0.8);
      return;
    }
    if (event.key.toLowerCase() === 'z') {
      event.preventDefault();
      window.dispatchEvent(new Event('camera-fit'));
      return;
    }
    const view = VIEW_DIRECTIONS[Number(event.key)];
    if (!/^[0-6]$/.test(event.key) || !view || getState().gizmo === 'color' && event.key !== '0') return;
    event.preventDefault();
    this.view(view);
  };

  setBed(area: THREE.Vector2[]) {
    this.area = area;
    if (this.handedOff) this.handedOff = false;
    else this.view('plate');
  }

  private takePose(from: CameraViews) {
    this.copy(from);
    this.handedOff = true;
  }

  fit(box: THREE.Box3) {
    if (box.isEmpty() || ![...box.min.toArray(), ...box.max.toArray()].every(Number.isFinite)) return this.view('plate');
    const sphere = box.getBoundingSphere(new THREE.Sphere());
    sphere.radius = Math.max(0.25, sphere.radius);
    const { min, max } = bedBounds(this.area);
    this.focus(sphere.center, Math.max(max.x - min.x, max.y - min.y, sphere.radius * 2, 1));
    const dir = this.camera.position.clone().sub(this.controls.target).normalize();
    this.controls.target.copy(sphere.center);
    if (this.camera instanceof THREE.OrthographicCamera) {
      this.camera.position.copy(sphere.center).add(dir.multiplyScalar(Math.max(1000, sphere.radius * 4)));
      this.camera.zoom = Math.min(this.camera.top, this.camera.right) / (sphere.radius * 1.1);
      this.camera.updateProjectionMatrix();
    } else {
      const half = THREE.MathUtils.degToRad(this.camera.fov / 2), fov = Math.min(half, Math.atan(Math.tan(half) * this.camera.aspect));
      this.camera.position.copy(sphere.center).add(dir.multiplyScalar((sphere.radius * 1.1) / Math.sin(fov)));
    }
    this.controls.update();
    this.changed();
  }

  shift(dx: number, dy: number) {
    const d = new THREE.Vector3(dx, dy, 0);
    this.camera.position.add(d);
    this.controls.target.add(d);
    this.controls.cursor.add(d);
    this.controls.update();
    this.changed();
  }

  zoom(factor: number) {
    if (this.camera instanceof THREE.OrthographicCamera) {
      this.camera.zoom = THREE.MathUtils.clamp(this.camera.zoom * factor, this.controls.minZoom, this.controls.maxZoom);
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
    if (view === 'plate') {
      this.controls.target.set(center.x, center.y, 0);
      this.focus(this.controls.target, size);
      this.camera.zoom = 1;
      if (this.camera instanceof THREE.OrthographicCamera) {
        this.camera.top = distance * Math.tan(THREE.MathUtils.degToRad(17.5));
        this.camera.bottom = -this.camera.top;
        this.resize(this.width, this.height);
        this.zoomLimits();
      }
    }
    const offsets = { plate: [0, -Math.SQRT1_2, Math.SQRT1_2], top: [0, -1e-4, 1], bottom: [0, -1e-4, -1],
      front: [0, -1, 0], rear: [0, 1, 0], left: [-1, 0, 0], right: [1, 0, 0] };
    this.axisView = false;
    this.camera.up.set(0, 0, 1);
    this.camera.position.copy(this.controls.target).add(new THREE.Vector3(...offsets[view] as [number, number, number]).normalize().multiplyScalar(distance));
    this.controls.update();
    this.clip();
    this.axisView = view !== 'plate';
    this.axisDirection.copy(this.camera.position).sub(this.controls.target).normalize();
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
    this.resize(this.width, this.height);
    this.zoomLimits();
    this.controls.update();
    this.clip();
    if (this.active()) cameraProjection(usePerspective);
    this.changed();
  }

  copy(from: CameraViews) {
    this.bounds.copy(from.bounds);
    this.projection(from.camera instanceof THREE.PerspectiveCamera);
    this.camera.position.copy(from.camera.position);
    this.camera.quaternion.copy(from.camera.quaternion);
    this.camera.up.copy(from.camera.up);
    this.camera.zoom = from.camera.zoom;
    if (this.camera instanceof THREE.OrthographicCamera && from.camera instanceof THREE.OrthographicCamera) {
      this.camera.top = from.camera.top;
      this.camera.bottom = from.camera.bottom;
    }
    this.axisView = from.axisView;
    this.axisDirection.copy(from.axisDirection);
    this.controls.target.copy(from.controls.target);
    this.focus(from.controls.cursor, from.focusRadius);
    this.resize(this.width, this.height);
    this.controls.update();
    this.clip();
    this.changed();
  }

  setInsets(left: number, right: number) {
    left = Math.max(0, Math.round(left)); right = Math.max(0, Math.round(right));
    if (left === this.insets.left && right === this.insets.right) return;
    this.insets = { left, right };
    this.resize(this.width, this.height);
    this.changed();
  }

  resize(width: number, height: number) {
    this.width = Math.max(1, width); this.height = Math.max(1, height);
    const full = Math.max(this.width / 2, this.width + this.insets.left - this.insets.right);
    if (this.camera instanceof THREE.PerspectiveCamera) this.camera.aspect = full / this.height;
    else {
      this.camera.right = this.camera.top * full / this.height;
      this.camera.left = -this.camera.right;
    }
    if (full !== this.width) this.camera.setViewOffset(full, this.height, 0, 0, this.width, this.height);
    else this.camera.view = null;
    this.camera.updateProjectionMatrix();
  }
  isActive() { return this.active(); }
  dispose() {
    registry.delete(this);
    if (lastActive === this) lastActive = null;
    window.removeEventListener('camera-command', this.onCommand);
    window.removeEventListener('keydown', this.onKey);
    this.unsubscribe();
    this.unsubscribePrefs();
    this.host.removeEventListener('pointermove', this.onTouchpadMove);
    this.host.removeEventListener('wheel', this.onWheel, { capture: true });
    this.controls.dispose();
  }
}
