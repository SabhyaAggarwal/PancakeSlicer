import { bakeStl, centre, hullPoints, parseObj, parseStl, vertexNormals, weld } from './meshcore';
import { buildBvh, type Bvh } from './bvh';
import { split } from './split';
import { computeMeshStats, type MeshStats } from './meshStatsCore';
import { readProject } from './threemf';
import { displayLod, type DisplayMesh } from './display-lod';
import { simplifyMesh, type SimplificationInfo } from './simplify-core';
import type { ShellsOut, ShellsIn } from './shells.worker';

export interface MeshData {
  display?: DisplayMesh;
  motion?: DisplayMesh;
  stats: MeshStats;
  contentSignature: string;
  simplification?: SimplificationInfo;
  positions: Float32Array;
  index: Uint32Array;
  normals: Float32Array;
  centre: [number, number, number];
  min: [number, number, number];
  max: [number, number, number];
  radius: number;
}

export type MeshIn =
  | { type: 'stats'; id: number; positions: Float32Array; index: Uint32Array | Uint16Array | null }
  | { type: 'split'; id: number; file: Blob }
  | { type: 'mesh'; id: number; file: File; simplify?: boolean }
  | { type: 'project'; id: number; file: File; partsOnly?: boolean }
  | { type: 'bake'; id: number; positions: Float32Array; index: Uint32Array | Uint16Array | null; matrix: number[] };

export type MeshOut =
  | { type: 'stats'; id: number; stats: MeshStats }
  | { type: 'pieces'; id: number; pieces: Blob[] }
  | { type: 'progress'; id: number; pct: number }
  | ({ type: 'mesh'; id: number; item?: number } & MeshData)
  | { type: 'shells'; id: number; item?: number; gap: number | null }
  | { type: 'hull'; id: number; item?: number; hull: Float32Array }
  | { type: 'bvh'; id: number; item?: number; bvh: Bvh; ms: number }
  | { type: 'items'; id: number; items: Awaited<ReturnType<typeof readProject>>['items']; materials: Awaited<ReturnType<typeof readProject>>['materials'] }
  | { type: 'missing'; id: number; item: number }
  | { type: 'blob'; id: number; blob: Blob }
  | { type: 'done'; id: number }
  | { type: 'error'; id: number; message: string };

const post = (m: MeshOut, transfer: Transferable[] = []) => (self as unknown as Worker).postMessage(m, transfer);
let shellsWorker: Worker | null = null;
let shellId = 0;
const shellRequests = new Map<number, { id: number; item?: number }>();
function checkShells(id: number, positions: Float32Array, index: Uint32Array, item?: number) {
  if (!shellsWorker) {
    shellsWorker = new Worker(new URL('./shells.worker.ts', import.meta.url), { type: 'module' });
    const fail = () => {
      for (const source of shellRequests.values()) post({ type: 'shells', ...source, gap: null });
      shellRequests.clear();
      shellsWorker?.terminate();
      shellsWorker = null;
    };
    shellsWorker.onerror = (e) => { e.preventDefault(); fail(); };
    shellsWorker.onmessageerror = fail;
    shellsWorker.onmessage = (e: MessageEvent<ShellsOut>) => {
      const source = shellRequests.get(e.data.id);
      shellRequests.delete(e.data.id);
      if (source) post({ type: 'shells', ...source, gap: e.data.gap });
    };
  }
  const request = ++shellId;
  shellRequests.set(request, { id, item });
  shellsWorker.postMessage({ id: request, positions, index } satisfies ShellsIn);
}

async function contentHash(array: Float32Array | Uint32Array): Promise<string> {
  if (globalThis.crypto?.subtle) {
    const digest = await crypto.subtle.digest('SHA-256', array.buffer as ArrayBuffer);
    return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
  }
  const words = new Uint32Array(array.buffer, array.byteOffset, array.length);
  let a = 0x811c9dc5, b = 0x9e3779b9, c = 0x85ebca6b, d = 0xc2b2ae35;
  for (const word of words) {
    a = Math.imul(a ^ word, 0x01000193);
    b = Math.imul(b ^ word, 0x85ebca6b);
    c = Math.imul(c ^ word, 0xc2b2ae35);
    d = Math.imul(d ^ word, 0x27d4eb2f);
  }
  return [words.length, a >>> 0, b >>> 0, c >>> 0, d >>> 0].map((n) => n.toString(16)).join('-');
}

async function finish(id: number, soup: Float32Array, item?: number, progress?: (pct: number) => void, simplify = false): Promise<() => void> {
  let { positions, index } = weld(soup);
  let simplification: SimplificationInfo | undefined;
  if (simplify) {
    const result = await simplifyMesh(positions, index).catch(() => null);
    if (result) { positions = result.positions; index = result.index; simplification = result.info; }
  }
  progress?.(75);
  const box = centre(positions);
  const stats = computeMeshStats(positions, index);
  const normals = vertexNormals(positions, index);
  let r2 = 0;
  const cx = (box.min[0] + box.max[0]) / 2, cy = (box.min[1] + box.max[1]) / 2, cz = (box.min[2] + box.max[2]) / 2;
  for (let i = 0; i < positions.length; i += 3)
    r2 = Math.max(r2, (positions[i] - cx) ** 2 + (positions[i + 1] - cy) ** 2 + (positions[i + 2] - cz) ** 2);
  const contentSignature = (await Promise.all([contentHash(positions), contentHash(index)])).join(':');
  const displayResult = await simplifyMesh(positions, index, { minTriangles: 100_000, target: 50_000 }).catch(() => null);
  const display = displayResult ? { positions: displayResult.positions, index: displayResult.index, normals: vertexNormals(displayResult.positions, displayResult.index) }
    : displayLod(positions, index, normals, box.min, box.max);
  const motionResult = await simplifyMesh(display?.positions ?? positions, display?.index ?? index,
    { minTriangles: 50_000, target: 12_500, maxErrorMm: 0.15 }).catch(() => null);
  const motion = motionResult ? { positions: motionResult.positions, index: motionResult.index, normals: vertexNormals(motionResult.positions, motionResult.index) } : undefined;
  checkShells(id, positions, index, item);
  const keep = positions.slice(), keepIndex = index.slice();
  const hullSource = keep.length <= 150_000 ? keep
    : display && display.positions.length <= 150_000 ? display.positions.slice() : null;
  post({ type: 'mesh', id, item, stats, positions, index, normals, display, motion, contentSignature, simplification, ...box, radius: Math.sqrt(r2) }, [positions.buffer, index.buffer, normals.buffer, ...(display ? [display.positions.buffer, display.index.buffer, display.normals.buffer] : []), ...(motion ? [motion.positions.buffer, motion.index.buffer, motion.normals.buffer] : [])]);
  return () => {
    const t0 = performance.now();
    const bvh = buildBvh(keep, keepIndex);
    post({ type: 'bvh', id, item, bvh, ms: performance.now() - t0 }, [bvh.bounds.buffer, bvh.nodes.buffer, bvh.tris.buffer]);
    const hull = hullSource ? hullPoints(hullSource) : new Float32Array(0);
    post({ type: 'hull', id, item, hull }, [hull.buffer]);
  };
}

self.onmessage = async (e: MessageEvent<MeshIn>) => {
  const m = e.data;
  try {
    if (m.type === 'stats') {
      post({ type: 'stats', id: m.id, stats: computeMeshStats(m.positions, m.index) });
    } else if (m.type === 'split') {
 post({ type: 'pieces', id: m.id, pieces: split(parseStl(await m.file.arrayBuffer())) });
 } else if (m.type === 'mesh') {
      const big = m.file.size > 4 << 20;
      const progress = big ? (pct: number) => post({ type: 'progress', id: m.id, pct }) : undefined;
      progress?.(0);
      const ext = m.file.name.toLowerCase().split('.').pop();
      if (ext !== 'stl' && ext !== 'obj') throw new Error(`${m.file.name}: STL, OBJ and 3MF are supported`);
      const soup = ext === 'stl' ? parseStl(await m.file.arrayBuffer()) : parseObj(await m.file.text());
      progress?.(35);
      (await finish(m.id, soup, undefined, progress, m.simplify !== false))();
    } else if (m.type === 'project') {
      const project = await readProject(m.file, m.partsOnly);
      post({ type: 'items', id: m.id, items: project.items, materials: project.materials });
      const hulls: (() => void)[] = [];
      for (let i = 0; i < project.items.length; i++) {
        const soup = project.mesh(i);
        if (soup) hulls.push(await finish(m.id, soup, i));
        else post({ type: 'missing', id: m.id, item: i });
      }
      for (const h of hulls) h();
    } else if (m.type === 'bake') {
      post({ type: 'blob', id: m.id, blob: bakeStl(m.positions, m.index, m.matrix) });
    }
    post({ type: 'done', id: m.id });
  } catch (err) {
    post({ type: 'error', id: m.id, message: err instanceof Error ? err.message : String(err) });
  }
};
