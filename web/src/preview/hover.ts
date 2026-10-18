import * as THREE from 'three';
import type { PreviewData } from './data';
import { MoveType } from './format';

const RADIUS = 6;

interface Grid {
  key: string;
  cell: number;
  columns: number;
  rows: number;
  start: Uint32Array;
  ids: Uint32Array;
  x: Float32Array;
  y: Float32Array;
  depth: Float32Array;
}

export interface RecordPatch {
  id: number;
  object_id?: number;
  kind?: string;
  shells?: number;
  area_mm2?: number;
  band?: number;
  mode_chosen?: string;
  explain?: string;
}

export interface HoverInfo {
  title: string;
  body: string;
}

export class NonplanarHover {
  private grid: Grid | null = null;

  pick(d: PreviewData, camera: THREE.PerspectiveCamera, w: number, h: number, range: [number, number], hiddenRoles: Set<number>, px: number, py: number): number {
    const m = new THREE.Matrix4().multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
    const key = `${m.elements.join(',')}|${w}|${h}|${range}|${[...hiddenRoles].join(',')}|${d.loaded}`;
    if (this.grid?.key !== key) this.grid = build(d, m, w, h, range, hiddenRoles, key);
    const g = this.grid;
    let best = -1, bestDepth = Infinity;
    const c0 = Math.max(0, Math.floor((px - RADIUS) / g.cell)), c1 = Math.min(g.columns - 1, Math.floor((px + RADIUS) / g.cell));
    const r0 = Math.max(0, Math.floor((py - RADIUS) / g.cell)), r1 = Math.min(g.rows - 1, Math.floor((py + RADIUS) / g.cell));
    for (let r = r0; r <= r1; ++r)
      for (let c = c0; c <= c1; ++c) {
        const cell = r * g.columns + c;
        for (let k = g.start[cell]; k < g.start[cell + 1]; ++k) {
          const dx = g.x[k] - px, dy = g.y[k] - py;
          if (dx * dx + dy * dy <= RADIUS * RADIUS && g.depth[k] < bestDepth) {
            bestDepth = g.depth[k];
            best = g.ids[k];
          }
        }
      }
    return best;
  }
}

function build(d: PreviewData, m: THREE.Matrix4, w: number, h: number, range: [number, number], hidden: Set<number>, key: string): Grid {
  const st = d.states!;
  const e = m.elements;
  const cell = 2 * RADIUS;
  const columns = Math.max(1, Math.ceil(w / cell)), rows = Math.max(1, Math.ceil(h / cell));
  const ids: number[] = [], xs: number[] = [], ys: number[] = [], ds: number[] = [];
  const end = Math.min(range[1], d.loaded);
  for (let i = Math.max(1, range[0]); i < end; ++i) {
    if ((d.type[i] & 0x7f) !== MoveType.Extrude || st.shell[d.state[i]] === 0xff || hidden.has(d.role[i])) continue;
    const x = d.pos[i * 4], y = d.pos[i * 4 + 1], z = d.pos[i * 4 + 2];
    const cw = e[3] * x + e[7] * y + e[11] * z + e[15];
    if (cw <= 0) continue;
    const cz = (e[2] * x + e[6] * y + e[10] * z + e[14]) / cw;
    if (Math.abs(cz) > 1) continue;
    const sx = ((e[0] * x + e[4] * y + e[8] * z + e[12]) / cw + 1) * 0.5 * w;
    const sy = (1 - (e[1] * x + e[5] * y + e[9] * z + e[13]) / cw) * 0.5 * h;
    if (sx < -RADIUS || sy < -RADIUS || sx > w + RADIUS || sy > h + RADIUS) continue;
    ids.push(i);
    xs.push(sx);
    ys.push(sy);
    ds.push(cz);
  }
  const cellOf = (k: number) =>
    Math.min(rows - 1, Math.max(0, Math.floor(ys[k] / cell))) * columns + Math.min(columns - 1, Math.max(0, Math.floor(xs[k] / cell)));
  const start = new Uint32Array(columns * rows + 1);
  for (let k = 0; k < ids.length; ++k) ++start[cellOf(k) + 1];
  for (let c = 1; c < start.length; ++c) start[c] += start[c - 1];
  const fill = start.slice(0, -1);
  const g: Grid = { key, cell, columns, rows, start, ids: new Uint32Array(ids.length), x: new Float32Array(ids.length), y: new Float32Array(ids.length), depth: new Float32Array(ids.length) };
  for (let k = 0; k < ids.length; ++k) {
    const at = fill[cellOf(k)]++;
    g.ids[at] = ids[k];
    g.x[at] = xs[k];
    g.y[at] = ys[k];
    g.depth[at] = ds[k];
  }
  return g;
}

export function explain(d: PreviewData, id: number, record: Record<string, unknown> | undefined): HoverInfo {
  const st = d.states!;
  const s = d.state[id];
  const patchId = st.patch[s];
  const objects = (record?.objects as { id?: number; name?: string; patches?: RecordPatch[] }[] | undefined) ?? [];
  const label = st.label[s];
  const own = objects.filter((o) => o.id === label);
  for (const o of own.length ? own : objects.length === 1 || label < 0 ? objects : []) {
    const p = o.patches?.find((q) => q.id === patchId);
    if (!p) continue;
    const name = o.name ?? 'object';
    const area = p.area_mm2 != null ? `${p.area_mm2.toFixed(0)} mm²` : '';
    if (p.kind === 'top' || p.kind == null)
      return { title: `Curved patch ${p.id} of ${name}`, body: [`${p.shells ?? '?'} shells over ${area}, band ${(p.band ?? -1) + 1}.`, p.explain ?? ''].filter(Boolean).join('\n') };
    return { title: `Curved ${p.kind} patch ${p.id} of ${name}`, body: `${p.shells ?? '?'} shells over ${area}.` };
  }
  return {
    title: `Curved pass of patch ${patchId}`,
    body: `Shell ${st.shell[s] + 1}, band ${st.band[s] + 1}. Printed curved where the head clears it.`,
  };
}
