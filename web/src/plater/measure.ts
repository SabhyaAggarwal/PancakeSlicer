import * as THREE from 'three';
import { centreOf, near } from './measureState';
import type { Edge, Feature } from './measureState';
export type { Feature } from './measureState';
export { centreOf };

type V = THREE.Vector3;

export const fmt = (v: number) => v.toFixed(3);

export function featureText(f: Feature | undefined): string {
  if (!f) return 'None';
  if (f.type === 'point') return f.label;
  if (f.type === 'edge') return `Edge (Length: ${fmt(f.a.distanceTo(f.b))}mm)`;
  if (f.type === 'circle') return `Circle (Diameter: ${fmt(2 * f.r)}mm)`;
  return 'Plane';
}

interface Topology {
  start: Uint32Array;
  tris: Uint32Array;
  plane: Int32Array;
  planes: LocalPlane[];
}
type LocalFeature = { type: 'edge'; a: V; b: V } | { type: 'circle'; c: V; r: number };
interface LocalPlane {
  normal: V;
  tris: number[];
  features: LocalFeature[];
  origin: V;
}

const topologies = new WeakMap<THREE.BufferGeometry, Topology>();

function topology(g: THREE.BufferGeometry): Topology {
  let t = topologies.get(g);
  if (t) return t;
  const index = g.index!.array;
  const nv = g.attributes.position.count, nt = index.length / 3;
  const start = new Uint32Array(nv + 1);
  for (let k = 0; k < index.length; k++) start[index[k] + 1]++;
  for (let v = 0; v < nv; v++) start[v + 1] += start[v];
  const fill = start.slice(0, nv);
  const tris = new Uint32Array(index.length);
  for (let k = 0; k < index.length; k++) tris[fill[index[k]]++] = (k / 3) | 0;
  t = { start, tris, plane: new Int32Array(nt).fill(-1), planes: [] };
  topologies.set(g, t);
  return t;
}

function faceNormal(p: ArrayLike<number>, index: ArrayLike<number>, f: number, out: V): V {
  const a = 3 * index[3 * f], b = 3 * index[3 * f + 1], c = 3 * index[3 * f + 2];
  const ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2];
  const vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
  return out.set(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx).normalize();
}

function neighbour(t: Topology, index: ArrayLike<number>, f: number, v0: number, v1: number): number {
  for (let k = t.start[v0]; k < t.start[v0 + 1]; k++) {
    const g = t.tris[k];
    if (g !== f && (index[3 * g] === v1 || index[3 * g + 1] === v1 || index[3 * g + 2] === v1)) return g;
  }
  return -1;
}

function fitCircle(pts: V[], toPlane: THREE.Quaternion): { c: V; r: number; err: number } {
  const q = pts.map((p) => p.clone().applyQuaternion(toPlane));
  const n = q.length;
  let best = { c: new THREE.Vector3(), r: 0, err: Infinity };
  for (let s = 0; s < Math.min(6, n); s++) {
    const [a, b, c] = [q[s], q[(s + Math.floor(n / 3)) % n], q[(s + Math.floor((2 * n) / 3)) % n]];
    const d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (Math.abs(d) < 1e-12) continue;
    const a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
    const cx = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    const cy = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    const r = q.reduce((m, p) => m + Math.hypot(p.x - cx, p.y - cy), 0) / n;
    const err = q.reduce((m, p) => Math.max(m, Math.abs(Math.hypot(p.x - cx, p.y - cy) - r)), 0);
    if (err < best.err) best = { c: new THREE.Vector3(cx, cy, q[0].z).applyQuaternion(toPlane.clone().invert()), r, err };
  }
  return best;
}

const approx = (a: number, b: number, eps = 0.01) => Math.abs(a - b) < eps;

function borderFeatures(border: V[], normal: V, toPlane: THREE.Quaternion, out: LocalFeature[]) {
  const n = border.length;
  if (n <= 1) return;
  if (n > 4) {
    const { c, r, err } = fitCircle(border, toPlane);
    if (err < 0.05) {
      const polygon = n <= 8;
      const lengthsMatch = border.slice(2).every((p, k) => approx(p.distanceToSquared(border[k + 1]), border[k + 1].distanceToSquared(border[k])));
      if (lengthsMatch && (polygon || n > 8)) {
        if (polygon) for (let j = 0; j < n; j++) out.push({ type: 'edge', a: border[(j + n - 1) % n], b: border[j] });
        else out.push({ type: 'circle', c, r });
        return;
      }
    }
  }
  const at = (i: number, o: number) => (((i + o) % n) + n) % n;
  const angles: number[] = [], lengths: number[] = [];
  let firstDifferent = 0;
  for (let i = 0; i < n; i++) {
    const v2 = border[i].clone().sub(border[at(i, -1)]), v1 = border[at(i, 1)].clone().sub(border[i]);
    let angle = Math.atan2(-normal.dot(v1.clone().cross(v2)), -v1.dot(v2)) + Math.PI;
    if (angle > Math.PI) angle = 2 * Math.PI - angle;
    angles.push(angle);
    lengths.push(v2.length());
    if (!firstDifferent && i > 0 && !approx(angles[i], angles[i - 1])) firstDifferent = i;
  }
  const arcs: [number, number][] = [];
  let startIdx = -1, inArc = false, first = true, run: V[] = [], runLength = 0;
  const firstPt = at(firstDifferent, 1);
  for (let i = firstPt; i !== firstPt || first; i = at(i, 1), first = false) {
    if (approx(angles[i], angles[at(i, -1)]) && i !== at(firstPt, -1) && i !== startIdx) {
      if (!inArc) {
        inArc = true;
        startIdx = at(i, -2);
        run = [border[startIdx], border[at(startIdx, 1)]];
        runLength = lengths[at(i, -1)];
      }
      run.push(border[i]);
      runLength += lengths[i];
    } else {
      if (inArc && run.length >= 5) {
        run.push(border[i]);
        runLength += lengths[i];
        let accept = true;
        for (let j = at(startIdx, 3); j !== i; j = at(j, 1)) if (!approx(lengths[at(j, -1)], lengths[j])) { accept = false; break; }
        if (accept) {
          const { c, r, err } = fitCircle(run, toPlane);
          if (err < 0.05 && runLength / r > 0.9 * Math.PI / 2) {
            arcs.push([startIdx, i]);
            out.push({ type: 'circle', c, r });
          }
        }
      }
      inArc = false;
    }
  }
  const edges: Edge[] = [];
  if (!arcs.length) for (let i = 0; i < n; i++) edges.push([border[at(i, -1)], border[i]]);
  else if (arcs.length > 1 || arcs[0][0] !== arcs[0][1]) {
    let i = arcs[0][1], k = 1;
    for (;;) {
      i = at(i, 1);
      edges.push([border[at(i, -1)], border[i]]);
      if (k < arcs.length && i === arcs[k][0]) i = arcs[k++][1];
      if (i === arcs[0][0]) break;
    }
  }
  for (let i = edges.length - 1; i >= 0 && edges.length > 1; i--) {
    const p = edges[i === 0 ? edges.length - 1 : i - 1], e = edges[i];
    if (near(p[1], e[0]) && approx(p[1].clone().sub(p[0]).normalize().dot(e[1].clone().sub(e[0]).normalize()), 1, 1e-4)) {
      edges[i === 0 ? edges.length - 1 : i - 1] = [p[0], e[1]];
      edges.splice(i, 1);
    }
  }
  for (const [a, b] of edges) out.push({ type: 'edge', a, b });
}

function planeOf(g: THREE.BufferGeometry, seed: number): LocalPlane {
  const t = topology(g);
  if (t.plane[seed] >= 0) return t.planes[t.plane[seed]];
  const p = g.attributes.position.array, index = g.index!.array;
  const id = t.planes.length;
  const normal = faceNormal(p, index, seed, new THREE.Vector3());
  const n = new THREE.Vector3();
  const same = (f: number) => {
    faceNormal(p, index, f, n);
    return Math.abs(n.x - normal.x) < 0.001 && Math.abs(n.y - normal.y) < 0.001 && Math.abs(n.z - normal.z) < 0.001;
  };
  const tris: number[] = [];
  const queue = [seed];
  t.plane[seed] = id;
  while (queue.length) {
    const f = queue.pop()!;
    tris.push(f);
    for (let j = 0; j < 3; j++) {
      const h = neighbour(t, index, f, index[3 * f + j], index[3 * f + (j + 1) % 3]);
      if (h >= 0 && t.plane[h] < 0 && same(h)) {
        t.plane[h] = id;
        queue.push(h);
      }
    }
  }
  const next = new Map<number, number>();
  for (const f of tris)
    for (let j = 0; j < 3; j++) {
      const v0 = index[3 * f + j], v1 = index[3 * f + (j + 1) % 3];
      const h = neighbour(t, index, f, v0, v1);
      if (h < 0 || t.plane[h] !== id) next.set(v0, v1);
    }
  const vec = (v: number) => new THREE.Vector3(p[3 * v], p[3 * v + 1], p[3 * v + 2]);
  const toPlane = new THREE.Quaternion().setFromUnitVectors(normal, new THREE.Vector3(0, 0, 1));
  const features: LocalFeature[] = [];
  const origin = new THREE.Vector3();
  let count = 0;
  while (next.size) {
    const [start] = next.keys();
    const loop: V[] = [];
    for (let v: number | undefined = start; v !== undefined && next.has(v); ) {
      loop.push(vec(v));
      const w: number = next.get(v)!;
      next.delete(v);
      v = w;
    }
    loop.forEach((q) => origin.add(q));
    count += loop.length;
    borderFeatures(loop, normal, toPlane, features);
  }
  const plane = { normal, tris, features, origin: count ? origin.divideScalar(count) : vec(index[3 * seed]) };
  t.planes.push(plane);
  return plane;
}

const closestOnSegment = (q: V, a: V, b: V) => new THREE.Line3(a, b).closestPointToPoint(q, true, new THREE.Vector3());

function pointCircle(q: V, c: V, r: number, normal: V): DistAndPoints {
  const n = normal.clone().normalize();
  const proj = q.clone().addScaledVector(n, -n.dot(q.clone().sub(c)));
  if (near(proj, c)) return { dist: r, from: c, to: c.clone().add(orthogonal(n).multiplyScalar(r)) };
  const to = c.clone().add(proj.clone().sub(c).normalize().multiplyScalar(r));
  return { dist: Math.hypot(proj.distanceTo(c) - r, q.distanceTo(proj)), from: q, to };
}

function orthogonal(v: V): V {
  const a = [v.x, v.y, v.z];
  const imax = [0, 1, 2].reduce((m, i) => (Math.abs(a[i]) > Math.abs(a[m]) ? i : m), 0), inext = (imax + 1) % 3;
  const r = [0, 0, 0];
  r[imax] = a[inext], r[inext] = -a[imax];
  return new THREE.Vector3(...r).normalize();
}

export function featureAt(g: THREE.BufferGeometry, tri: number, point: V, world: THREE.Matrix4, key: string, pointOnly: boolean): Feature {
  const plane = planeOf(g, tri);
  const w = (v: V) => v.clone().applyMatrix4(world);
  const normals = new THREE.Matrix3().getNormalMatrix(world);
  let best: LocalFeature | null = null, min = 0.5;
  for (const f of plane.features) {
    const d = f.type === 'edge' ? closestOnSegment(point, f.a, f.b).distanceTo(point) : pointCircle(point, f.c, f.r, plane.normal).dist;
    if (d < min) min = d, best = f;
  }
  if (pointOnly) return { type: 'point', p: w(point), label: `Point on ${best?.type ?? 'plane'}` };
  if (best?.type === 'edge') {
    const limit = Math.max(0.025, Math.min(0.5, 0.1 * best.a.distanceTo(best.b)));
    if (point.distanceTo(best.a) < limit) return { type: 'point', p: w(best.a), label: 'Vertex' };
    if (point.distanceTo(best.b) < limit) return { type: 'point', p: w(best.b), label: 'Vertex' };
    return { type: 'edge', a: w(best.a), b: w(best.b) };
  }
  if (best?.type === 'circle') {
    const c = w(best.c);
    const r = w(best.c.clone().add(orthogonal(plane.normal).multiplyScalar(best.r))).distanceTo(c);
    return { type: 'circle', c, r, normal: plane.normal.clone().applyMatrix3(normals).normalize() };
  }
  const p = g.attributes.position.array, index = g.index!.array;
  const tris = new Float32Array(plane.tris.length * 9);
  const v = new THREE.Vector3();
  plane.tris.forEach((f, k) => {
    for (let j = 0; j < 3; j++) {
      const i = index[3 * f + j];
      v.set(p[3 * i], p[3 * i + 1], p[3 * i + 2]).applyMatrix4(world).toArray(tris, 9 * k + 3 * j);
    }
  });
  return {
    type: 'plane', normal: plane.normal.clone().applyMatrix3(normals).normalize(), origin: w(plane.origin), tris,
    edges: plane.features.flatMap((f) => (f.type === 'edge' ? [[w(f.a), w(f.b)] as Edge] : [])), id: `${key}:${plane.tris[0]}`,
  };
}

export interface DistAndPoints { dist: number; from: V; to: V }
export interface Measurement {
  angle?: number;
  distanceInfinite?: DistAndPoints;
  distanceStrict?: DistAndPoints;
  distanceXyz?: V;
}

const EPSILON = 1e-4;
const parallel = (u: V, v: V) => Math.abs(Math.abs(u.clone().normalize().dot(v.clone().normalize())) - 1) < EPSILON;
const perpendicular = (u: V, v: V) => Math.abs(u.clone().normalize().dot(v.clone().normalize())) < EPSILON;
const dp = (from: V, to: V): DistAndPoints => ({ dist: from.distanceTo(to), from, to });
const nearest = (ds: DistAndPoints[]) => ds.reduce((a, b) => (b.dist < a.dist ? b : a));
const toPlane = (q: V, normal: V, origin: V): DistAndPoints => {
  const n = normal.clone().normalize();
  return dp(q, q.clone().addScaledVector(n, -n.dot(q.clone().sub(origin))));
};

function angleEdgeEdge(e1: Edge, e2: Edge): number {
  const d1 = e1[1].clone().sub(e1[0]), d2 = e2[1].clone().sub(e2[0]);
  if (parallel(d1, d2)) return 0;
  const w = e1[0].clone().sub(e2[0]);
  const a = d1.dot(d1), b = d1.dot(d2), c = d2.dot(d2), d = d1.dot(w), e = d2.dot(w);
  const den = a * c - b * b;
  const centre = e1[0].clone().addScaledVector(d1, (b * e - c * d) / den).add(e2[0].clone().addScaledVector(d2, (a * e - b * d) / den)).multiplyScalar(0.5);
  const away = (s: Edge, dir: V) => (centre.distanceToSquared(s[0]) > centre.distanceToSquared(s[1]) ? dir.negate() : dir).normalize();
  return Math.acos(THREE.MathUtils.clamp(away(e1, d1).dot(away(e2, d2)), -1, 1));
}

function pointEdge(q: V, [s, e]: Edge): Measurement {
  const line = new THREE.Line3(s, e);
  const t = line.closestPointToPointParameter(q, false);
  const inf = dp(q, line.at(t, new THREE.Vector3()));
  return { distanceStrict: t > 0 && t < 1 ? inf : dp(q, t <= 0 ? s : e), distanceInfinite: inf };
}

function edgeCircleNearest([s, e]: Edge, c: V, r: number, normal: V): DistAndPoints {
  const ds = [pointCircle(s, c, r, normal), pointCircle(e, c, r, normal)];
  const dir = e.clone().sub(s);
  const t = dir.dot(c.clone().sub(s)) / dir.lengthSq();
  if (t >= 0 && t < 1) ds.push(pointCircle(s.clone().addScaledVector(dir, t), c, r, normal));
  return nearest(ds);
}

function measureOrdered(f1: Feature, f2: Feature, centres: boolean): Measurement {
  if (f1.type === 'point') {
    if (f2.type === 'point') return { distanceStrict: dp(f1.p, f2.p), distanceXyz: f2.p.clone().sub(f1.p) };
    if (f2.type === 'edge') return pointEdge(f1.p, [f2.a, f2.b]);
    if (f2.type === 'circle') {
      const d = pointCircle(f1.p, f2.c, f2.r, f2.normal);
      return { distanceStrict: d.from === f2.c || !centres ? d : dp(f1.p, f2.c) };
    }
    return { distanceInfinite: toPlane(f1.p, f2.normal, f2.origin) };
  }
  if (f1.type === 'edge') {
    const e1: Edge = [f1.a, f1.b];
    if (f2.type === 'edge') {
      const e2: Edge = [f2.a, f2.b];
      const ds = [dp(e1[0], e2[0]), dp(e1[0], e2[1]), dp(e1[1], e2[0]), dp(e1[1], e2[1])];
      const foot = (v: V, e: Edge, flip: boolean) => {
        const line = new THREE.Line3(e[0], e[1]);
        const t = line.closestPointToPointParameter(v, false);
        if (t >= 0 && t < 1) ds.push(flip ? dp(line.at(t, new THREE.Vector3()), v) : dp(v, line.at(t, new THREE.Vector3())));
      };
      foot(e1[0], e2, false), foot(e1[1], e2, false), foot(e2[0], e1, true), foot(e2[1], e1, true);
      return { distanceInfinite: nearest(ds), angle: angleEdgeEdge(e1, e2) };
    }
    if (f2.type === 'circle') {
      const d = edgeCircleNearest(e1, f2.c, f2.r, f2.normal);
      return { distanceInfinite: centres ? dp(d.from, f2.c) : d };
    }
    if (f2.type !== 'plane') return {};
    const dir = f1.b.clone().sub(f1.a);
    let distanceInfinite: DistAndPoints | undefined;
    if (perpendicular(dir, f2.normal)) distanceInfinite = nearest([toPlane(f1.a, f2.normal, f2.origin), toPlane(f1.b, f2.normal, f2.origin)]);
    else {
      const ds = f2.edges.map((e) => measureOrdered({ type: 'edge', a: e[0], b: e[1] }, f1, centres).distanceInfinite);
      if (ds.length && ds.every(Boolean)) distanceInfinite = nearest(ds as DistAndPoints[]);
    }
    const angle = perpendicular(dir, f2.normal) ? 0 : Math.asin(THREE.MathUtils.clamp(Math.abs(dir.clone().normalize().dot(f2.normal.clone().normalize())), 0, 1));
    return { distanceInfinite, angle };
  }
  if (f1.type === 'circle') {
    if (f2.type === 'circle') return { distanceStrict: dp(f1.c, f2.c) };
    if (f2.type !== 'plane') return {};
    const coplanar = parallel(f1.normal, f2.normal) && toPlane(f2.origin, f1.normal, f1.c).dist < EPSILON;
    if (coplanar) return { distanceStrict: dp(f1.c, f2.origin) };
    const ds = f2.edges.map((e) => edgeCircleNearest(e, f1.c, f1.r, f1.normal)).map((d) => ({ dist: d.dist, from: d.to, to: d.from }));
    return { distanceInfinite: ds.length ? nearest(ds) : toPlane(f1.c, f2.normal, f2.origin) };
  }
  if (f1.type === 'plane' && f2.type === 'plane') {
    if (parallel(f1.normal, f2.normal)) return { distanceInfinite: toPlane(f1.origin, f2.normal, f2.origin) };
    const dir = f1.normal.clone().cross(f2.normal).normalize();
    const n1 = f1.normal.clone().normalize(), n2 = f2.normal.clone().normalize();
    const d1 = n1.dot(f1.origin), d2 = n2.dot(f2.origin), k = n1.dot(n2);
    const o = n1.clone().multiplyScalar((d1 - d2 * k) / (1 - k * k)).add(n2.clone().multiplyScalar((d2 - d1 * k) / (1 - k * k)));
    const half = (q: V) => { const r = q.clone().sub(o); return r.addScaledVector(dir, -r.dot(dir)).normalize(); };
    return { angle: Math.acos(THREE.MathUtils.clamp(half(f1.origin).dot(half(f2.origin)), -1, 1)) };
  }
  return {};
}

const RANK = { point: 0, edge: 1, circle: 2, plane: 3 };

export function measure(a: Feature, b: Feature): Measurement {
  if (RANK[a.type] <= RANK[b.type]) return measureOrdered(a, b, true);
  const m = measureOrdered(b, a, true);
  const swap = (d?: DistAndPoints) => d && { dist: d.dist, from: d.to, to: d.from };
  return { ...m, distanceInfinite: swap(m.distanceInfinite), distanceStrict: swap(m.distanceStrict) };
}

export function measureRows(m: Measurement): [label: string, value: string][] {
  const rows: [string, string][] = [];
  if (m.angle !== undefined) rows.push(['Angle', `${fmt(THREE.MathUtils.radToDeg(m.angle))}°`]);
  const strict = !!m.distanceStrict && (!m.distanceInfinite || Math.abs(m.distanceStrict.dist - m.distanceInfinite.dist) > EPSILON);
  if (m.distanceInfinite) rows.push([strict ? 'Perpendicular distance' : 'Distance', `${fmt(m.distanceInfinite.dist)}mm`]);
  if (strict) rows.push(['Direct distance', `${fmt(m.distanceStrict!.dist)}mm`]);
  if (m.distanceXyz && m.distanceXyz.length() > EPSILON)
    rows.push(['Distance XYZ', `(${fmt(m.distanceXyz.x)}, ${fmt(m.distanceXyz.y)}, ${fmt(m.distanceXyz.z)})`]);
  return rows;
}

export function distanceComponents(m: Measurement): V | null {
  const v = m.distanceXyz && m.distanceXyz.length() > EPSILON ? m.distanceXyz
    : m.distanceInfinite ? m.distanceInfinite.to.clone().sub(m.distanceInfinite.from)
    : m.distanceStrict ? m.distanceStrict.to.clone().sub(m.distanceStrict.from) : null;
  return v && v.length() > 0.01 ? v : null;
}
