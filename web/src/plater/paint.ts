import * as THREE from 'three';
import { setState, type PlateObject, type State } from '../state/store';

export type PaintKind = 'support' | 'seam' | 'fuzzy';
export type BrushTool = 'circle' | 'sphere' | 'fill';
export type PaintNode = number | PaintNode[];
export type Painting = Map<number, PaintNode>;

const COLOURS = [null, [0.5, 1, 0.5], [1, 0.5, 0.5]] as const;
const MAX_DEPTH = 8;

export const triangleCount = (g: THREE.BufferGeometry) => (g.index ? g.index.count : g.attributes.position.count) / 3;
const corner = (g: THREE.BufferGeometry, t: number, k: number) => (g.index ? g.index.getX(3 * t + k) : 3 * t + k);
type Tri = [THREE.Vector3, THREE.Vector3, THREE.Vector3];
const vertices = (g: THREE.BufferGeometry, t: number): Tri =>
  [0, 1, 2].map((k) => new THREE.Vector3().fromBufferAttribute(g.attributes.position, corner(g, t, k))) as Tri;
const mid = (a: THREE.Vector3, b: THREE.Vector3) => a.clone().add(b).multiplyScalar(0.5);
const children = ([a, b, c]: Tri): Tri[] => {
  const ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
  return [[a, ab, ca], [ab, b, bc], [bc, c, ca], [ab, bc, ca]];
};

export function paintOf(o: PlateObject, kind: PaintKind): Painting | null {
  const p = o.paint?.[kind];
  if (!p || !p.size) return null;
  const T = triangleCount(o.geometry);
  for (const t of p.keys()) if (t >= T) return null;
  return p;
}

const neighbourCache = new WeakMap<THREE.BufferGeometry, Int32Array>();
function neighbours(g: THREE.BufferGeometry): Int32Array {
  let n = neighbourCache.get(g);
  if (n) return n;
  const T = triangleCount(g);
  n = new Int32Array(3 * T).fill(-1);
  const edges = new Map<string, number>();
  for (let t = 0; t < T; t++)
    for (let k = 0; k < 3; k++) {
      const a = corner(g, t, k), b = corner(g, t, (k + 1) % 3);
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      const other = edges.get(key);
      if (other === undefined) edges.set(key, 3 * t + k);
      else { n[3 * t + k] = Math.floor(other / 3); n[other] = t; edges.delete(key); }
    }
  neighbourCache.set(g, n);
  return n;
}

const normalCache = new WeakMap<THREE.BufferGeometry, Float32Array>();
function faceNormals(g: THREE.BufferGeometry): Float32Array {
  let out = normalCache.get(g);
  if (out) return out;
  const T = triangleCount(g);
  out = new Float32Array(3 * T);
  for (let t = 0; t < T; t++) {
    const [a, b, c] = vertices(g, t);
    c.sub(b).cross(a.sub(b)).normalize();
    out[3 * t] = c.x, out[3 * t + 1] = c.y, out[3 * t + 2] = c.z;
  }
  normalCache.set(g, out);
  return out;
}
const normalOf = (g: THREE.BufferGeometry, t: number) => { const n = faceNormals(g); return new THREE.Vector3(n[3 * t], n[3 * t + 1], n[3 * t + 2]); };

function grow(g: THREE.BufferGeometry, seed: number, accept: (t: number, from: number) => boolean): number[] {
  const n = neighbours(g);
  const seen = new Uint8Array(triangleCount(g));
  const out = [seed], stack = [seed];
  seen[seed] = 1;
  while (stack.length) {
    const t = stack.pop()!;
    for (let k = 0; k < 3; k++) {
      const u = n[3 * t + k];
      if (u < 0 || seen[u] || !accept(u, t)) continue;
      seen[u] = 1;
      out.push(u);
      stack.push(u);
    }
  }
  return out;
}

class Cursor {
  private readonly d: THREE.Vector3 | null;
  constructor(readonly centre: THREE.Vector3, readonly radius: number, tool: 'circle' | 'sphere', view: THREE.Vector3) {
    this.d = tool === 'circle' ? view.clone().normalize() : null;
  }
  private flat(p: THREE.Vector3) {
    const v = p.clone().sub(this.centre);
    return this.d ? v.addScaledVector(this.d, -v.dot(this.d)) : v;
  }
  inside = (p: THREE.Vector3) => this.flat(p).lengthSq() <= this.radius * this.radius;
  faces = (n: THREE.Vector3) => !this.d || n.dot(this.d) < 0;
  touches([a, b, c]: Tri): boolean {
    const [pa, pb, pc] = [a, b, c].map((p) => this.flat(p));
    const r2 = this.radius * this.radius;
    const seg = (p: THREE.Vector3, q: THREE.Vector3) => {
      const e = q.clone().sub(p), t = Math.min(1, Math.max(0, -p.dot(e) / (e.lengthSq() || 1)));
      return p.clone().addScaledVector(e, t).lengthSq() <= r2;
    };
    if (seg(pa, pb) || seg(pb, pc) || seg(pc, pa)) return true;
    const n = pb.clone().sub(pa).cross(pc.clone().sub(pa));
    const s1 = pb.clone().sub(pa).cross(pa.clone().negate()).dot(n), s2 = pc.clone().sub(pb).cross(pb.clone().negate()).dot(n), s3 = pa.clone().sub(pc).cross(pc.clone().negate()).dot(n);
    return (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
  }
}

function brushNode(node: PaintNode, tri: Tri, cursor: Cursor, state: number, limit2: number, depth: number): PaintNode {
  const inside = tri.map(cursor.inside);
  if (inside.every(Boolean)) return state;
  if (!inside.some(Boolean) && !cursor.touches(tri)) return node;
  const long = Math.max(tri[0].distanceToSquared(tri[1]), tri[1].distanceToSquared(tri[2]), tri[2].distanceToSquared(tri[0]));
  if (typeof node === 'number' && (long <= limit2 || depth >= MAX_DEPTH)) {
    const c = tri[0].clone().add(tri[1]).add(tri[2]).multiplyScalar(1 / 3);
    return cursor.inside(c) ? state : node;
  }
  const kids = typeof node === 'number' ? [node, node, node, node] : node;
  const next = children(tri).map((t, i) => brushNode(kids[i], t, cursor, state, limit2, depth + 1));
  return next.every((k) => k === next[0] && typeof k === 'number') ? next[0] : next;
}

const sameNode = (a: PaintNode | undefined, b: PaintNode | undefined): boolean =>
  (a ?? 0) === (b ?? 0) || Array.isArray(a) && Array.isArray(b) && a.every((k, i) => sameNode(k, b[i]));

function update(painting: Painting | null, tris: number[], paint: (node: PaintNode, t: number) => PaintNode): Painting | null {
  let next: Painting | null = null;
  for (const t of tris) {
    const before = painting?.get(t) ?? 0;
    const after = paint(before, t);
    if (sameNode(before, after)) continue;
    next ??= new Map(painting ?? []);
    if (after === 0) next.delete(t); else next.set(t, after);
  }
  return next ?? painting;
}

export function brush(g: THREE.BufferGeometry, painting: Painting | null, hit: number, point: THREE.Vector3, radius: number,
  tool: 'circle' | 'sphere', view: THREE.Vector3, state: 0 | 1 | 2): Painting | null {
  const cursor = new Cursor(point, radius, tool, view);
  const limit = radius / 3, limit2 = limit * limit;
  const tris = grow(g, hit, (t) => cursor.faces(normalOf(g, t)) && cursor.touches(vertices(g, t)));
  return update(painting, tris.filter((t) => cursor.faces(normalOf(g, t))), (node, t) => brushNode(node, vertices(g, t), cursor, state, limit2, 0));
}

export function fill(g: THREE.BufferGeometry, painting: Painting | null, hit: number, angle: number, state: 0 | 1 | 2): Painting | null {
  const nrm = faceNormals(g), limit = Math.cos((angle * Math.PI) / 180) - 1e-9;
  const tris = grow(g, hit, (t, from) => nrm[3 * t] * nrm[3 * from] + nrm[3 * t + 1] * nrm[3 * from + 1] + nrm[3 * t + 2] * nrm[3 * from + 2] >= limit);
  return update(painting, tris, () => state);
}

const overlayCache = new WeakMap<Painting, THREE.BufferGeometry>();
export function overlayGeometry(g: THREE.BufferGeometry, painting: Painting): THREE.BufferGeometry {
  let out = overlayCache.get(painting);
  if (out) return out;
  const pos: number[] = [], col: number[] = [];
  const emit = (node: PaintNode, tri: Tri) => {
    if (Array.isArray(node)) return children(tri).forEach((t, i) => emit(node[i], t));
    const colour = COLOURS[node];
    if (!colour) return;
    for (const v of tri) pos.push(v.x, v.y, v.z), col.push(...colour);
  };
  for (const [t, node] of painting) emit(node, vertices(g, t));
  out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  out.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  overlayCache.set(painting, out);
  return out;
}

export function paintString(node: PaintNode): string {
  const nibbles: number[] = [];
  const put = (n: PaintNode) => {
    if (Array.isArray(n)) { nibbles.push(0b0011); for (let i = 3; i >= 0; i--) put(n[i]); }
    else nibbles.push((n & 0b11) << 2);
  };
  put(node);
  return nibbles.reverse().map((v) => v.toString(16).toUpperCase()).join('');
}

export function paintJson(o: PlateObject): Record<string, Record<string, string>> | undefined {
  const out: Record<string, Record<string, string>> = {};
  for (const kind of ['support', 'seam', 'fuzzy'] as const) {
    const painting = paintOf(o, kind);
    if (!painting) continue;
    const degenerate: number[] = [];
    for (let t = 0, T = triangleCount(o.geometry); t < T; t++) {
      const a = corner(o.geometry, t, 0), b = corner(o.geometry, t, 1), c = corner(o.geometry, t, 2);
      if (a === b || b === c || a === c) degenerate.push(t);
    }
    const before = (t: number) => { let lo = 0, hi = degenerate.length; while (lo < hi) { const m = (lo + hi) >> 1; if (degenerate[m] < t) lo = m + 1; else hi = m; } return lo; };
    const map: Record<string, string> = {};
    for (const [t, node] of [...painting].sort((x, y) => x[0] - y[0]))
      if (!degenerate.includes(t)) map[String(t - before(t))] = paintString(node);
    if (Object.keys(map).length) out[kind] = map;
  }
  return Object.keys(out).length ? out : undefined;
}

export interface PaintTool { tool: BrushTool; radius: number; angle: number }
export const RADIUS = { min: 0.4, max: 8, step: 0.2 } as const;
export const ANGLE = { min: 0, max: 90, step: 5 } as const;
let tool: PaintTool = { tool: 'circle', radius: 1, angle: 30 };
const listeners = new Set<() => void>();
export const paintTool = () => tool;
export function setPaintTool(patch: Partial<PaintTool>) {
  tool = { ...tool, ...patch };
  tool.radius = Math.min(RADIUS.max, Math.max(RADIUS.min, Math.round(tool.radius * 100) / 100));
  tool.angle = Math.min(ANGLE.max, Math.max(ANGLE.min, tool.angle));
  listeners.forEach((l) => l());
}
export const subscribePaintTool = (l: () => void) => (listeners.add(l), () => void listeners.delete(l));

export const canPaint = (s: State) => !!s.features.paint && !!s.selected && !s.selected.all && !s.project &&
  !!s.objects.find((o) => o.id === s.selected!.object && !o.project);

export const togglePaint = (kind: PaintKind) => setState((s) => ({ gizmo: s.gizmo === kind || !canPaint(s) ? 'none' : kind }));

export const isPaint = (gizmo: string): gizmo is PaintKind => gizmo === 'support' || gizmo === 'seam' || gizmo === 'fuzzy';

export const PAINT_STEP: Record<PaintKind, string> = {
  support: 'Paint-on supports editing', seam: 'Paint-on seam editing', fuzzy: 'Paint-on fuzzy skin editing',
};
