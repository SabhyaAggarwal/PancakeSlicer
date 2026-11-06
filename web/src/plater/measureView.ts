



import * as THREE from 'three';
import { LineSegments2 } from 'three/examples/jsm/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/examples/jsm/lines/LineSegmentsGeometry.js';
import { LineMaterial } from 'three/examples/jsm/lines/LineMaterial.js';
import { centreOf, featureAt, fmt, measure, type Feature } from './measure';
import { measureState, sameFeature, selectFeature, setMeasure } from './measureState';

const srgb = (r: number, g: number, b: number) => new THREE.Color().setRGB(r, g, b, THREE.SRGBColorSpace);

const SELECTED_1ST = srgb(0.25, 0.75, 0.75), SELECTED_2ND = srgb(0.75, 0.25, 0.75), NEUTRAL = srgb(0.5, 0.5, 0.5), HOVER = srgb(0, 1, 0);
const TRIANGLE_BASE = 10, TRIANGLE_HEIGHT = TRIANGLE_BASE * 1.618033;



export interface MeshHit { mesh: THREE.Mesh; tri: number; local: THREE.Vector3 }

export class MeasureView {
  readonly scene = new THREE.Scene();
  private svg: SVGSVGElement;
  private dot: THREE.Texture;
  private size = new THREE.Vector2(1, 1);
  private pointMode = false;
  private dims: { from: THREE.Vector3; to: THREE.Vector3; dist: number } | null = null;

  constructor(host: HTMLElement, private camera: () => THREE.Camera) {
    this.svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    Object.assign(this.svg.style, { position: 'absolute', inset: '0', width: '100%', height: '100%', pointerEvents: 'none', overflow: 'visible' });
    host.appendChild(this.svg);
    const c = document.createElement('canvas');
    c.width = c.height = 32;
    const g = c.getContext('2d')!;
    g.fillStyle = '#fff';
    g.beginPath();
    g.arc(16, 16, 15, 0, 2 * Math.PI);
    g.fill();
    this.dot = new THREE.CanvasTexture(c);
  }

  dispose() {
    this.clear();
    this.dot.dispose();
    this.svg.remove();
  }

  resize(w: number, h: number) {
    this.size.set(w, h);
  }

  private screen(p: THREE.Vector3): THREE.Vector2 {
    const v = p.clone().project(this.camera());
    return new THREE.Vector2((v.x + 1) / 2 * this.size.x, (1 - v.y) / 2 * this.size.y);
  }





  hover(hit: MeshHit | null, at: THREE.Vector2, shift: boolean) {
    this.pointMode = shift;
    const s = measureState();
    let f: Feature | undefined;
    if (!shift)
      for (const g of [s.hover, s.first, s.second]) {
        const c = centreOf(g?.type === 'point' ? undefined : g);
        if (c && c.type === 'point' && this.screen(c.p).distanceTo(at) < 10) { f = c; break; }
      }
    if (!f && hit) f = featureAt(hit.mesh.geometry, hit.tri, hit.local, hit.mesh.matrixWorld, String(hit.mesh.userData.instance), shift);
    if (!sameFeature(f, s.hover) || f?.type === 'point' && shift) setMeasure({ ...s, hover: f });
  }


  click(): boolean {
    const f = measureState().hover;
    if (!f) return false;
    selectFeature(f);
    return true;
  }

  private clear() {
    for (const o of this.scene.children as (THREE.Mesh | THREE.Points | LineSegments2)[]) {
      o.geometry.dispose();
      (o.material as THREE.Material).dispose();
    }
    this.scene.clear();
  }


  sync() {
    this.clear();
    const { hover, first, second } = measureState();
    const hoverColour = this.pointMode ? HOVER : !first || sameFeature(hover, first) ? SELECTED_1ST : SELECTED_2ND;
    const draw = (f: Feature | undefined, colour: THREE.Color, withCentre: boolean) => {
      if (!f) return;
      if (f.type === 'point') return this.points([f.p], colour);
      if (f.type === 'plane') {
        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.BufferAttribute(f.tris, 3));
        this.scene.add(new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color: colour, side: THREE.DoubleSide, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -4 })));
        return;
      }
      const seg: number[] = [];
      if (f.type === 'edge') seg.push(...f.a.toArray(), ...f.b.toArray());
      else {
        const u = new THREE.Vector3(1, 0, 0);
        if (Math.abs(u.dot(f.normal)) > 0.9) u.set(0, 1, 0);
        u.addScaledVector(f.normal, -u.dot(f.normal)).normalize();
        const v = f.normal.clone().cross(u);
        const at = (k: number) => f.c.clone().addScaledVector(u, f.r * Math.cos(k * Math.PI / 32)).addScaledVector(v, f.r * Math.sin(k * Math.PI / 32));
        for (let k = 0; k < 64; k++) seg.push(...at(k).toArray(), ...at(k + 1).toArray());
      }
      this.lines(seg, colour, 4);
      const c = centreOf(f);
      if (withCentre && c?.type === 'point') this.points([c.p], NEUTRAL);
    };
    draw(first, SELECTED_1ST, true);
    draw(second, SELECTED_2ND, true);
    if (!sameFeature(hover, first) && !sameFeature(hover, second)) draw(hover, hoverColour, true);

    if (hover?.type === 'point' && /^Center/.test(hover.label)) this.points([hover.p], hoverColour);

    this.dims = null;
    if (first && second) {
      const m = measure(first, second);
      const d = m.distanceInfinite ?? m.distanceStrict;
      if (d && d.dist >= 0.001) this.dims = d;
    }
  }

  private lines(seg: number[], colour: THREE.Color, width: number) {
    const geo = new LineSegmentsGeometry().setPositions(seg);
    const mat = new LineMaterial({ color: colour, linewidth: width, depthTest: false, transparent: true });
    mat.resolution.copy(this.size);
    const l = new LineSegments2(geo, mat);
    l.renderOrder = 1;
    this.scene.add(l);
  }

  private points(ps: THREE.Vector3[], colour: THREE.Color) {
    const geo = new THREE.BufferGeometry().setFromPoints(ps);
    const p = new THREE.Points(geo, new THREE.PointsMaterial({ color: colour, size: 10, sizeAttenuation: false, map: this.dot, alphaTest: 0.5, depthTest: false, transparent: true }));
    p.renderOrder = 2;
    this.scene.add(p);
  }


  frame() {
    for (const o of this.scene.children) if (o instanceof LineSegments2) (o.material as LineMaterial).resolution.copy(this.size);
    this.svg.replaceChildren();
    if (!this.dims) return;
    const { from, to, dist } = this.dims;
    const x = from.clone().setX(to.x), y = x.clone().setY(to.y);
    this.arrow(from, x, '#ff0000', false, false);
    this.arrow(x, y, '#00ff00', false, false);
    this.arrow(y, to, '#0000ff', false, false);
    this.arrow(from, to, '#ffffff', true, true, `${fmt(dist)} mm`);
  }


  private arrow(a3: THREE.Vector3, b3: THREE.Vector3, colour: string, label: boolean, firstTip: boolean, text = '') {
    if (a3.distanceToSquared(b3) < 1e-6) return;
    const a = this.screen(a3), b = this.screen(b3);
    const len = a.distanceTo(b);
    if (len < 1e-3) return;
    const u = b.clone().sub(a).divideScalar(len), n = new THREE.Vector2(-u.y, u.x);
    const overlap = len - 2 * TRIANGLE_HEIGHT < 0;
    const ns = 'http://www.w3.org/2000/svg';
    const el = (tag: string, attrs: Record<string, string | number>) => {
      const e = document.createElementNS(ns, tag);
      for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, String(v));
      this.svg.appendChild(e);
      return e;
    };
    const s0 = overlap ? a.clone().addScaledVector(u, -2 * TRIANGLE_HEIGHT) : a, s1 = overlap ? b.clone().addScaledVector(u, 2 * TRIANGLE_HEIGHT) : b;
    el('line', { x1: s0.x, y1: s0.y, x2: s1.x, y2: s1.y, stroke: colour, 'stroke-width': 2 });
    const tip = (p: THREE.Vector2, dir: THREE.Vector2) => {
      const back = p.clone().addScaledVector(dir, -TRIANGLE_HEIGHT);
      const pts = [p, back.clone().addScaledVector(n, TRIANGLE_BASE / 2), back.clone().addScaledVector(n, -TRIANGLE_BASE / 2)];
      el('polygon', { points: pts.map((q) => `${q.x},${q.y}`).join(' '), fill: colour });
    };
    if (firstTip) tip(a, overlap ? u : u.clone().negate());
    tip(b, overlap ? u.clone().negate() : u);
    if (label) {
      const m = a.clone().add(b).multiplyScalar(0.5);
      const t = el('text', { x: m.x + 4, y: m.y - 4, fill: 'var(--text)', 'font-size': 13 }) as SVGTextElement;
      t.textContent = text;
      const box = t.getBBox();
      const bg = el('rect', { x: box.x - 3, y: box.y - 1, width: box.width + 6, height: box.height + 2, fill: 'rgba(255,255,255,0.5)' });
      this.svg.insertBefore(bg, t);
    }
  }
}
