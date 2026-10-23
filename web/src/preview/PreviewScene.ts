import * as THREE from 'three';
import { CameraViews } from '../plater/CameraViews';
import { getState } from '../state/store';
import type { SystemPreset } from '../api/types';
import { bedKey, buildBed, sceneColors } from '../plater/Bed';
import { subscribeTheme } from '../theme';
import { pixelRatio, watchPixelRatio } from '../plater/pixelRatio';
import { instanceMatrix, parsePoints } from '../plater/geometry';
import type { PlateObject } from '../state/store';
import { colorVertices, type ColorContext } from './colors';
import type { PreviewData } from './data';
import { SegmentRenderer, type DrawState, type FrameStats } from './renderer';
import { binOf, type SurfaceGrid } from './surface-grid';

const SURFACE_LIFT_MM = 0.15;

const FRAME_BUDGET_MS = 14;

export class PreviewScene {
  readonly renderer: THREE.WebGLRenderer;
  readonly scene = new THREE.Scene();
  readonly views: CameraViews;
  get camera() { return this.views.camera; }
  get controls() { return this.views.controls; }
  private segments: SegmentRenderer;
  private bed: THREE.Group | null = null;
  private bedKey = '';
  private machine: SystemPreset | undefined;
  private unsubTheme = subscribeTheme(() => {
    this.renderer.setClearColor(sceneColors().background);
    this.shellMat.color.set(sceneColors().shell);
    this.setBed(this.machine);
    this.request();
  });
  private area: THREE.Vector2[] = [];
  private shells = new THREE.Group();
  private shellMat = new THREE.MeshStandardMaterial({ color: sceneColors().shell, transparent: true, opacity: 0.35, depthWrite: false, roughness: 0.8 });
  private tool: THREE.Mesh;
  private surface: { mesh: THREE.Mesh; cells: Uint32Array; grid: SurfaceGrid } | null = null;
  private data: PreviewData | null = null;
  private uploaded = 0;
  private draw: DrawState = { range: [0, 0], roleMask: -1, typeMask: 0, optionMask: 0 };
  private frame = 0;
  private colorCtx: ColorContext | null = null;
  private timer: { ext: any; query: WebGLQuery | null; gpuMs: number } = { ext: null, query: null, gpuMs: NaN };
  lastFrame: FrameStats & { frameMs: number; gpuMs: number; scale: number } = { blocks: 0, culled: 0, lodBlocks: 0, segments: 0, cpuMs: 0, frameMs: 0, gpuMs: NaN, scale: 1 };
  onFrame: (() => void) | null = null;
  private moving = false;
  private scale = 1;
  private basePixelRatio = 1;
  private unwatchRatio = watchPixelRatio(() => this.resize());

  constructor(private host: HTMLElement) {
    this.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
    this.renderer.setClearColor(sceneColors().background);
    this.renderer.autoClear = true;
    host.appendChild(this.renderer.domElement);
    this.scene.add(new THREE.HemisphereLight(0xffffff, 0x8a8a8a, 1.6));
    const sun = new THREE.DirectionalLight(0xffffff, 1.6);
    sun.position.set(-0.4, -0.6, 1);
    this.scene.add(sun);
    this.scene.add(this.shells);
    this.views = new CameraViews(this.renderer.domElement, () => getState().tab === 'preview', () => this.request());
    this.controls.addEventListener('change', () => this.request());
    this.controls.addEventListener('start', () => (this.moving = true));
    this.controls.addEventListener('end', () => {
      this.moving = false;
      this.request();
    });
    const cone = new THREE.ConeGeometry(1.2, 4, 24);
    cone.rotateX(-Math.PI / 2);
    cone.translate(0, 0, 2);
    this.tool = new THREE.Mesh(cone, new THREE.MeshStandardMaterial({ color: 0xffffff, transparent: true, opacity: 0.6 }));
    this.tool.visible = false;
    this.scene.add(this.tool);
    this.segments = new SegmentRenderer(this.renderer);
    const gl = this.renderer.getContext() as WebGL2RenderingContext;
    this.timer.ext = gl.getExtension('EXT_disjoint_timer_query_webgl2');
    new ResizeObserver(() => this.resize()).observe(host);
    this.resize();
  }

  resize() {
    const w = this.host.clientWidth, h = this.host.clientHeight;
    if (!w || !h) return;
    const size = this.renderer.getSize(new THREE.Vector2());
    const base = pixelRatio(w, h);
    if (base !== this.basePixelRatio || size.x !== w || size.y !== h) {
      this.basePixelRatio = base;
      this.renderer.setPixelRatio(base * this.scale);
      this.renderer.setSize(w, h);
    }
    this.views.resize(w, h);
    this.request();
  }

  setBed(machine: SystemPreset | undefined) {
    this.machine = machine;
    const key = bedKey(machine);
    if (key === this.bedKey) return;
    this.bedKey = key;
    if (this.bed) this.scene.remove(this.bed);
    this.bed = buildBed(machine, () => this.request());
    this.scene.add(this.bed);
    this.area = parsePoints(machine?.config.printable_area);
    this.views.setBed(this.area);
  }

  setShells(objects: PlateObject[], visible: boolean) {
    this.shells.clear();
    if (visible)
      for (const o of objects)
        for (const inst of o.instances) {
          const m = new THREE.Mesh(o.geometry, this.shellMat);
          m.matrixAutoUpdate = false;
          m.matrix.copy(instanceMatrix(o, inst));
          m.renderOrder = 10;
          this.shells.add(m);
        }
    this.request();
  }

  setData(d: PreviewData) {
    this.data = d;
    this.uploaded = 0;
    this.segments.reset(d);
    this.request();
  }

  clear() {
    this.data = null;
    this.uploaded = 0;
    this.tool.visible = false;
    this.request();
  }

  holds(d: PreviewData) {
    return this.data === d;
  }

  sync() {
    const d = this.data;
    if (!d || d.loaded <= this.uploaded) return;
    const start = this.uploaded;
    this.segments.addChunk(start, d.loaded - start);
    if (this.colorCtx) {
      colorVertices(d, this.colorCtx, this.segments.colors, start, d.loaded);
      this.segments.uploadColors(start, d.loaded);
    }
    this.uploaded = d.loaded;
    this.request();
  }

  setColors(ctx: ColorContext) {
    this.colorCtx = ctx;
    const d = this.data;
    if (!d) return;
    colorVertices(d, ctx, this.segments.colors, 0, d.loaded);
    this.segments.uploadColors(0, d.loaded);
    this.request();
  }

  setDrawState(s: DrawState, toolAt: number | null) {
    this.draw = s;
    const d = this.data;
    if (d && toolAt != null && toolAt < d.loaded) {
      this.tool.position.set(d.pos[toolAt * 4], d.pos[toolAt * 4 + 1], d.pos[toolAt * 4 + 2]);
      this.tool.visible = true;
    } else this.tool.visible = false;
    this.request();
  }

  copyCamera(from: PreviewScene) {
    this.views.projection(from.camera instanceof THREE.PerspectiveCamera);
    const c = this.camera, f = from.camera;
    c.position.copy(f.position);
    c.quaternion.copy(f.quaternion);
    c.up.copy(f.up);
    c.zoom = f.zoom;
    if (c instanceof THREE.OrthographicCamera && f instanceof THREE.OrthographicCamera) (c.top = f.top), (c.bottom = f.bottom);
    this.controls.target.copy(from.controls.target);
    this.views.resize(Math.max(1, this.host.clientWidth), Math.max(1, this.host.clientHeight));
    this.request();
  }

  fit(min: THREE.Vector3, max: THREE.Vector3) {
    const centre = min.clone().add(max).multiplyScalar(0.5);
    const radius = Math.max(1, min.distanceTo(max) / 2) * 1.4;
    const c = this.camera;
    const dir = c.position.clone().sub(this.controls.target).normalize();
    this.controls.target.copy(centre);
    if (c instanceof THREE.PerspectiveCamera) {
      const half = THREE.MathUtils.degToRad(c.fov / 2);
      c.position.copy(centre).addScaledVector(dir, radius / Math.sin(Math.min(half, Math.atan(Math.tan(half) * c.aspect))));
    } else {
      c.position.copy(centre).addScaledVector(dir, Math.max(radius * 4, c.position.distanceTo(centre)));
      c.zoom = Math.min(c.top, c.right) / radius;
    }
    c.updateProjectionMatrix();
    this.controls.update();
    this.request();
  }

  setSurface(grid: SurfaceGrid | null) {
    if (this.surface?.grid === grid) return;
    if (this.surface) {
      this.scene.remove(this.surface.mesh);
      this.surface.mesh.geometry.dispose();
      (this.surface.mesh.material as THREE.Material).dispose();
      this.surface = null;
    }
    if (grid) {
      const { nx, ny, pitch, origin, rms, meshZ, eligible } = grid;
      const cells: number[] = [];
      for (let i = 0; i < nx * ny; ++i) if (eligible[i] > 0 && Number.isFinite(rms[i]) && Number.isFinite(meshZ[i])) cells.push(i);
      const pos = new Float32Array(cells.length * 18), col = new Float32Array(cells.length * 18);
      cells.forEach((i, k) => {
        const x0 = origin[0] + (i % nx) * pitch, y0 = origin[1] + Math.floor(i / nx) * pitch, z = meshZ[i] + SURFACE_LIFT_MM;
        const [r, g, b] = binOf(rms[i] * 1000).color;
        const c = new THREE.Color().setRGB(r / 255, g / 255, b / 255, THREE.SRGBColorSpace);
        const corners = [[x0, y0], [x0 + pitch, y0], [x0 + pitch, y0 + pitch], [x0, y0], [x0 + pitch, y0 + pitch], [x0, y0 + pitch]];
        corners.forEach(([x, y], v) => {
          pos.set([x, y, z], k * 18 + v * 3);
          col.set([c.r, c.g, c.b], k * 18 + v * 3);
        });
      });
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.BufferAttribute(pos, 3));
      geometry.setAttribute('color', new THREE.BufferAttribute(col, 3));
      const material = new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide });
      const mesh = new THREE.Mesh(geometry, material);
      this.scene.add(mesh);
      this.surface = { mesh, cells: Uint32Array.from(cells), grid };
    }
    this.request();
  }

  pickSurface(px: number, py: number): number {
    const s = this.surface;
    if (!s) return -1;
    const ray = new THREE.Raycaster();
    ray.setFromCamera(new THREE.Vector2((px / this.host.clientWidth) * 2 - 1, -(py / this.host.clientHeight) * 2 + 1), this.camera);
    const hit = ray.intersectObject(s.mesh)[0];
    return hit?.faceIndex != null ? s.cells[hit.faceIndex >> 1] : -1;
  }

  get segmentRenderer() {
    return this.segments;
  }

  set lod(on: boolean) {
    this.segments.lodEnabled = on;
    this.request();
  }

  gpuBytes() {
    return this.segments.gpuBytes();
  }

  request() {
    if (this.frame || !this.host.clientWidth) return;
    this.frame = requestAnimationFrame(() => {
      this.frame = 0;
      this.renderNow();
    });
  }

  renderNow() {
    const t0 = performance.now();
    const gl = this.renderer.getContext() as WebGL2RenderingContext;
    const tq = this.timer;
    if (tq.ext && tq.query && gl.getQueryParameter(tq.query, gl.QUERY_RESULT_AVAILABLE)) {
      if (!gl.getParameter(tq.ext.GPU_DISJOINT_EXT)) tq.gpuMs = gl.getQueryParameter(tq.query, gl.QUERY_RESULT) / 1e6;
      gl.deleteQuery(tq.query);
      tq.query = null;
    }
    const cost = Number.isFinite(tq.gpuMs) ? tq.gpuMs : this.lastFrame.frameMs;
    const scale = !this.moving ? 1 : cost > FRAME_BUDGET_MS * 1.5 ? Math.max(0.5, this.scale * 0.75) : this.scale;
    if (scale !== this.scale) {
      this.scale = scale;
      this.renderer.setPixelRatio(this.basePixelRatio * scale);
      this.renderer.setSize(this.host.clientWidth, this.host.clientHeight);
    }
    const timing = tq.ext && !tq.query;
    if (timing) {
      tq.query = gl.createQuery();
      gl.beginQuery(tq.ext.TIME_ELAPSED_EXT, tq.query!);
    }
    this.shells.visible = false;
    this.renderer.render(this.scene, this.camera);
    if (this.data) this.segments.render(this.camera, this.draw, this.renderer.domElement.height);
    if (this.shells.children.length) {
      this.shells.visible = true;
      const bedVisible = this.bed?.visible;
      if (this.bed) this.bed.visible = false;
      const tv = this.tool.visible;
      this.tool.visible = false;
      this.renderer.autoClear = false;
      this.renderer.render(this.scene, this.camera);
      this.renderer.autoClear = true;
      if (this.bed) this.bed.visible = bedVisible!;
      this.tool.visible = tv;
    }
    if (timing) gl.endQuery(tq.ext.TIME_ELAPSED_EXT);
    this.lastFrame = { ...this.segments.stats, frameMs: performance.now() - t0, gpuMs: tq.gpuMs, scale: this.scale };
    this.onFrame?.();
  }

  bench(frames = 120, zoom = 1): Promise<{ scale: number; medianIntervalMs: number; p95IntervalMs: number; medianFrameMs: number; p95FrameMs: number; medianGpuMs: number; segments: number; lodBlocks: number; blocks: number }> {
    const c = this.controls;
    const target = c.target.clone();
    const offset = this.camera.position.clone().sub(target).multiplyScalar(zoom);
    const intervals: number[] = [], cpu: number[] = [], gpu: number[] = [];
    let last = 0, i = 0;
    this.moving = true;
    return new Promise((resolve) => {
      const step = (t: number) => {
        if (last) intervals.push(t - last);
        last = t;
        const a = (i / frames) * Math.PI * 2;
        const o = offset.clone().applyAxisAngle(new THREE.Vector3(0, 0, 1), a);
        this.camera.position.copy(target).add(o);
        this.camera.lookAt(target);
        const f0 = performance.now();
        this.renderNow();
        (this.renderer.getContext() as WebGL2RenderingContext).finish();
        cpu.push(performance.now() - f0);
        if (Number.isFinite(this.lastFrame.gpuMs)) gpu.push(this.lastFrame.gpuMs);
        if (++i <= frames) requestAnimationFrame(step);
        else {
          const med = (v: number[]) => (v.length ? [...v].sort((x, y) => x - y)[Math.floor(v.length / 2)] : NaN);
          const p95 = (v: number[]) => (v.length ? [...v].sort((x, y) => x - y)[Math.floor(v.length * 0.95)] : NaN);
          this.moving = false;
          this.request();
          c.update();
          resolve({ scale: this.lastFrame.scale, medianIntervalMs: med(intervals), p95IntervalMs: p95(intervals), medianFrameMs: med(cpu), p95FrameMs: p95(cpu), medianGpuMs: med(gpu),
            segments: this.lastFrame.segments, lodBlocks: this.lastFrame.lodBlocks, blocks: this.lastFrame.blocks });
        }
      };
      requestAnimationFrame(step);
    });
  }

  dispose() {
    this.unsubTheme();
    this.unwatchRatio();
    cancelAnimationFrame(this.frame);
    this.segments.dispose();
    this.views.dispose();
    this.renderer.dispose();
    this.host.removeChild(this.renderer.domElement);
  }
}
