




import * as THREE from 'three';
import type { Config } from '../api/types';
import { objectMatrix } from '../plater/geometry';
import { effectiveConfig, getState, subscribeStore, type PlateObject, type State } from '../state/store';
import { num } from './goals';

export interface MeshStats {
  volume: number; side: number; up: number; down: number; height: number;

  tops: number; curvedTops: number; curvedArea: number;
}

const cache = new WeakMap<THREE.BufferGeometry, { key: string; stats: MeshStats }>();

export function meshStats(o: PlateObject): MeshStats {
  const key = `${o.rotX},${o.rotY},${o.scale}`;
  const hit = cache.get(o.geometry);
  if (hit?.key === key) return hit.stats;
  const m = objectMatrix(o);
  const pos = o.geometry.getAttribute('position') as THREE.BufferAttribute;
  const index = o.geometry.index;
  const n = index ? index.count / 3 : pos.count / 3;
  const vi = (t: number, k: number) => (index ? index.getX(3 * t + k) : 3 * t + k);
  const p = [new THREE.Vector3(), new THREE.Vector3(), new THREE.Vector3()];
  const e1 = new THREE.Vector3(), e2 = new THREE.Vector3(), nrm = new THREE.Vector3();

  const weld = new Map<string, number>();
  const parent: number[] = [];
  const find = (x: number): number => { while (parent[x] !== x) x = parent[x] = parent[parent[x]]; return x; };
  const upTri: { v: number[]; area: number; sloped: boolean }[] = [];
  let volume = 0, side = 0, up = 0, down = 0, zmin = Infinity, zmax = -Infinity;
  for (let t = 0; t < n; t++) {
    const ids: number[] = [];
    for (let k = 0; k < 3; k++) {
      p[k].fromBufferAttribute(pos, vi(t, k)).applyMatrix4(m);
      zmin = Math.min(zmin, p[k].z); zmax = Math.max(zmax, p[k].z);
      const w = `${Math.round(p[k].x * 1e3)},${Math.round(p[k].y * 1e3)},${Math.round(p[k].z * 1e3)}`;
      let id = weld.get(w);
      if (id === undefined) { id = parent.length; weld.set(w, id); parent.push(id); }
      ids.push(id);
    }
    volume += p[0].dot(nrm.crossVectors(p[1], p[2])) / 6;
    nrm.crossVectors(e1.subVectors(p[1], p[0]), e2.subVectors(p[2], p[0]));
    const area = nrm.length() / 2;
    if (!area) continue;
    const nz = nrm.z / (2 * area);
    if (nz >= 0.5) up += area; else if (nz <= -0.5) down += area; else side += area;
    if (nz > 0.25) upTri.push({ v: ids, area, sloped: nz < 0.996 });
  }
  for (const t of upTri) { const a = find(t.v[0]); parent[find(t.v[1])] = a; parent[find(t.v[2])] = a; }
  const regions = new Map<number, { area: number; sloped: number }>();
  for (const t of upTri) {
    const r = find(t.v[0]);
    const g = regions.get(r) ?? { area: 0, sloped: 0 };
    g.area += t.area; if (t.sloped) g.sloped += t.area;
    regions.set(r, g);
  }
  let tops = 0, curvedTops = 0, curvedArea = 0;
  for (const g of regions.values()) {
    if (g.area < 4) continue;
    tops++;
    if (g.sloped > 0.3 * g.area) { curvedTops++; curvedArea += g.sloped; }
  }
  const stats = { volume: Math.abs(volume), side, up, down, height: zmax - zmin, tops, curvedTops, curvedArea };
  cache.set(o.geometry, { key, stats });
  return stats;
}

export interface Cost { time_s: number; grams: number }


function proxy(objects: PlateObject[], c: Config, fil: Config): Cost {
  const lh = num(c.layer_height) || 0.2;
  const lw = num(c.line_width) || num(c.inner_wall_line_width) || 0.42;
  const walls = num(c.wall_loops) || 2;
  const infill = num(c.sparse_infill_density) / 100;
  const top = num(c.top_shell_layers), bottom = num(c.bottom_shell_layers);
  const mvs = num(fil.filament_max_volumetric_speed) || 12;
  const density = num(fil.filament_density) || 1.24;
  const speed = (k: string, d: number) => Math.min(num(c[k]) || d, mvs / (lw * lh));
  let vol = 0, t = 0;
  for (const o of objects) {
    const st = meshStats(o);
    const copies = o.instances.filter((i) => i.printable !== false).length || 1;
    const wallV = Math.min(st.side * walls * lw, st.volume);
    const skinV = Math.min(st.up * top * lh + st.down * bottom * lh, Math.max(st.volume - wallV, 0));
    const infV = Math.max(st.volume - wallV - skinV, 0) * infill;
    const outer = wallV / walls, inner = wallV - outer;
    const sec = outer / (lw * lh * speed('outer_wall_speed', 120)) + inner / (lw * lh * speed('inner_wall_speed', 150))
      + skinV / (lw * lh * speed('internal_solid_infill_speed', 150)) + infV / (lw * lh * speed('sparse_infill_speed', 150))
      + (st.height / lh) * 2.5;
    vol += copies * (wallV + skinV + infV);
    t += copies * sec;
  }
  return { time_s: t, grams: (vol / 1000) * density };
}


let scale = { time: 1, grams: 1 };
const realOf = (s: State): Cost | null => {
  const r = s.job.state === 'done' && !s.job.stale ? s.job.status?.record : undefined;
  const time = Number((r?.text_summary as Record<string, unknown> | undefined)?.estimated_time_s);
  const grams = Number((r?.statistics as Record<string, unknown> | undefined)?.total_weight_g);
  return Number.isFinite(time) && Number.isFinite(grams) && time > 0 && grams > 0 ? { time_s: time, grams } : null;
};
let lastRecord: unknown = null;
subscribeStore(() => {
  const s = getState();
  const r = s.job.status?.record;
  if (r === lastRecord) return;
  lastRecord = r;
  const real = realOf(s);
  if (!real || !s.objects.length) return;
  const p = proxy(s.objects, effectiveConfig(s, 'process'), effectiveConfig(s, 'filament'));
  const t = real.time_s / p.time_s, g = real.grams / p.grams;
  scale = t > 0.3 && t < 3 && g > 0.3 && g < 3 ? { time: t, grams: g } : { time: 1, grams: 1 };
});


export function estimate(s: State, patch: Partial<State>): { now: Cost; next: Cost } | null {
  const objects = s.objects.filter((o) => !o.project);
  if (!objects.length) return null;
  const fil = effectiveConfig(s, 'filament');
  const a = proxy(objects, effectiveConfig(s, 'process'), fil);
  const b = proxy(objects, effectiveConfig({ ...s, ...patch }, 'process'), fil);
  const real = realOf(s);
  const k = (c: Cost): Cost => ({ time_s: c.time_s * scale.time, grams: c.grams * scale.grams });
  return { now: real ?? k(a), next: real ? { time_s: real.time_s * b.time_s / a.time_s, grams: real.grams * b.grams / a.grams } : k(b) };
}
