import * as THREE from 'three';
import { CameraViews } from '../plater/CameraViews';
import { getState } from '../state/store';
import type { SystemPreset } from '../api/types';
import { buildBed } from '../plater/Bed';
import { instanceMatrix, parsePoints } from '../plater/geometry';
import type { PlateObject } from '../state/store';
import { colorVertices, type ColorContext } from './colors';
import type { PreviewData } from './data';
import { SegmentRenderer, type DrawState, type FrameStats } from './renderer';

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
  private area: THREE.Vector2[] = [];
  private shells = new THREE.Group();
  private shellMat = new THREE.MeshStandardMaterial({ color: 0xc8c8c8, transparent: true, opacity: 0.35, depthWrite: false, roughness: 0.8 });
  private tool: THREE.Mesh;
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
  private readonly basePixelRatio = Math.min(window.devicePixelRatio, 2);

  constructor(private host: HTMLElement) {
    this.renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(this.basePixelRatio);
    this.renderer.setClearColor(0xe9e9e9);
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
    if (size.x !== w || size.y !== h) this.renderer.setSize(w, h);
    this.views.resize(w, h);
    this.request();
  }

  setBed(machine: SystemPreset | undefined) {
    const key = `${machine?.name}|${machine?.config.printable_area}|${machine?.config.bed_custom_texture}|${machine?.config.bed_custom_model}`;
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
    this.segments.render(this.camera, this.draw, this.renderer.domElement.height);
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
    cancelAnimationFrame(this.frame);
    this.segments.dispose();
    this.views.dispose();
    this.renderer.dispose();
    this.host.removeChild(this.renderer.domElement);
  }
}
