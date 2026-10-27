import { useEffect } from 'react';
import * as THREE from 'three';
import { getState, setState, useStore, type PlateObject } from '../state/store';
import { instanceBox, instanceMatrix } from '../plater/geometry';

type Stats = { volume: number; triangles: number; openEdges: number };
const cache = new WeakMap<THREE.BufferGeometry, Stats>();

function meshStats(g: THREE.BufferGeometry): Stats {
  let st = cache.get(g);
  if (st) return st;
  const p = g.attributes.position.array, index = g.index?.array;
  const n = index ? index.length / 3 : p.length / 9, at = (k: number) => (index ? index[k] : k);
  const edges = new Map<number, number>(), count = p.length / 3;
  let volume = 0;
  for (let t = 0; t < n; t++) {
    const a = at(3 * t), b = at(3 * t + 1), c = at(3 * t + 2);
    volume += (p[3 * a] * (p[3 * b + 1] * p[3 * c + 2] - p[3 * b + 2] * p[3 * c + 1]) - p[3 * a + 1] * (p[3 * b] * p[3 * c + 2] - p[3 * b + 2] * p[3 * c])
      + p[3 * a + 2] * (p[3 * b] * p[3 * c + 1] - p[3 * b + 1] * p[3 * c])) / 6;
    for (const [u, v] of [[a, b], [b, c], [c, a]]) {
      const key = Math.min(u, v) * count + Math.max(u, v);
      edges.set(key, (edges.get(key) ?? 0) + 1);
    }
  }
  let openEdges = 0;
  for (const k of edges.values()) if (k === 1) openEdges++;
  st = { volume: Math.abs(volume), triangles: n, openEdges };
  cache.set(g, st);
  return st;
}

function objectStats(o: PlateObject): Stats {
  const det = (m: THREE.Matrix4) => Math.abs(m.determinant());
  const own = meshStats(o.geometry), scale = det(instanceMatrix(o, o.instances[0]));
  const total = { volume: own.volume * scale, triangles: own.triangles, openEdges: own.openEdges };
  for (const v of o.volumes ?? []) {
    if (v.type !== 'part') continue;
    const vs = meshStats(v.geometry);
    total.volume += vs.volume * scale * det(new THREE.Matrix4().fromArray(v.matrix));
    total.triangles += vs.triangles;
    total.openEdges += vs.openEdges;
  }
  return total;
}

const num = (x: number) => String(Number(x.toPrecision(6)));
const idle = (f: () => void) => (typeof requestIdleCallback === 'function' ? requestIdleCallback(f) : setTimeout(f, 50));

export default function ObjectInfo() {
  const objects = useStore((s) => s.objects);
  const sel = useStore((s) => s.selected);
  const gizmo = useStore((s) => s.gizmo);
  const fix = useStore((s) => !!s.features.mesh_repair);
  useEffect(() => {
    let stop = false;
    const queue = objects.filter((o) => !o.project?.placeholder && !o.native);
    const next = () => {
      const o = queue.shift();
      if (stop || !o) return;
      const k = objectStats(o).openEdges, prev = getState().meshErrors;
      if ((prev[o.id] ?? 0) !== k) setState({ meshErrors: { ...prev, [o.id]: k } });
      idle(next);
    };
    idle(next);
    return () => { stop = true; };
  }, [objects]);
  const o = sel && !sel.all && sel.volume === undefined && (gizmo === 'none' || gizmo === 'move' || gizmo === 'rotate' || gizmo === 'scale')
    ? objects.find((ob) => ob.id === sel.object) : undefined;
  if (!o || o.project?.placeholder || o.native) return null;
  const st = objectStats(o), inst = o.instances.find((i) => i.id === sel!.instance) ?? o.instances[0];
  const size = instanceBox(o, inst).getSize(new THREE.Vector3());
  return (
    <div className="object-info" role="status" aria-label="Object info">
      <div>Object name: {o.name}</div>
      <div>Size: {num(size.x)} x {num(size.y)} x {num(size.z)} mm</div>
      <div>Volume: {num(st.volume)} mm³</div>
      <div>Triangles: {st.triangles}</div>
      {st.openEdges > 0 && <div className="object-info-error">
        Error: {st.openEdges} non-manifold edge{st.openEdges === 1 ? '' : 's'}.{'\n'}Tips:{'\n'}Use "Fix Model" to repair the mesh.
        {fix && <button type="button" className="notif-link" onClick={() => void import('../plater/meshOps').then((m) => m.fixModel(o.id))}>Fix Model</button>}
      </div>}
    </div>
  );
}
