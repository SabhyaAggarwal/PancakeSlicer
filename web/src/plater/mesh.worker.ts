import { bakeStl, centre, hullPoints, parseObj, parseStl, vertexNormals, weld } from './meshcore';
import { buildBvh, type Bvh } from './bvh';
import { split } from './split';
import { readProject } from './threemf';

export interface MeshData {
  positions: Float32Array;
  index: Uint32Array;
  normals: Float32Array;
  centre: [number, number, number];
  min: [number, number, number];
  max: [number, number, number];
  radius: number;
}

export type MeshIn =
  | { type: 'split'; id: number; file: Blob }
  | { type: 'mesh'; id: number; file: File }
  | { type: 'project'; id: number; file: File }
  | { type: 'bake'; id: number; positions: Float32Array; index: Uint32Array | Uint16Array | null; matrix: number[] };

export type MeshOut =
  | { type: 'pieces'; id: number; pieces: Blob[] }
  | { type: 'progress'; id: number; pct: number }
  | ({ type: 'mesh'; id: number; item?: number } & MeshData)
  | { type: 'hull'; id: number; item?: number; hull: Float32Array }
  | { type: 'bvh'; id: number; item?: number; bvh: Bvh; ms: number }
  | { type: 'items'; id: number; items: Awaited<ReturnType<typeof readProject>>['items']; materials: Awaited<ReturnType<typeof readProject>>['materials'] }
  | { type: 'missing'; id: number; item: number }
  | { type: 'blob'; id: number; blob: Blob }
  | { type: 'done'; id: number }
  | { type: 'error'; id: number; message: string };

const post = (m: MeshOut, transfer: Transferable[] = []) => (self as unknown as Worker).postMessage(m, transfer);

function finish(id: number, soup: Float32Array, item?: number, progress?: (pct: number) => void): () => void {
  const { positions, index } = weld(soup);
  progress?.(75);
  const box = centre(positions);
  const normals = vertexNormals(positions, index);
  let r2 = 0;
  const cx = (box.min[0] + box.max[0]) / 2, cy = (box.min[1] + box.max[1]) / 2, cz = (box.min[2] + box.max[2]) / 2;
  for (let i = 0; i < positions.length; i += 3)
    r2 = Math.max(r2, (positions[i] - cx) ** 2 + (positions[i + 1] - cy) ** 2 + (positions[i + 2] - cz) ** 2);
  const keep = positions.slice(), keepIndex = index.slice();
  post({ type: 'mesh', id, item, positions, index, normals, ...box, radius: Math.sqrt(r2) }, [positions.buffer, index.buffer, normals.buffer]);
  return () => {
    const t0 = performance.now();
    const bvh = buildBvh(keep, keepIndex);
    post({ type: 'bvh', id, item, bvh, ms: performance.now() - t0 }, [bvh.bounds.buffer, bvh.nodes.buffer, bvh.tris.buffer]);
    const hull = hullPoints(keep);
    post({ type: 'hull', id, item, hull }, [hull.buffer]);
  };
}

self.onmessage = async (e: MessageEvent<MeshIn>) => {
  const m = e.data;
  try {
    if (m.type === 'split') {
 post({ type: 'pieces', id: m.id, pieces: split(parseStl(await m.file.arrayBuffer())) });
 } else if (m.type === 'mesh') {
      const big = m.file.size > 4 << 20;
      const progress = big ? (pct: number) => post({ type: 'progress', id: m.id, pct }) : undefined;
      progress?.(0);
      const ext = m.file.name.toLowerCase().split('.').pop();
      if (ext !== 'stl' && ext !== 'obj') throw new Error(`${m.file.name}: STL, OBJ and 3MF are supported`);
      const soup = ext === 'stl' ? parseStl(await m.file.arrayBuffer()) : parseObj(await m.file.text());
      progress?.(35);
      finish(m.id, soup, undefined, progress)();
    } else if (m.type === 'project') {
      const project = await readProject(m.file);
      post({ type: 'items', id: m.id, items: project.items, materials: project.materials });
      const hulls: (() => void)[] = [];
      for (let i = 0; i < project.items.length; i++) {
        const soup = project.mesh(i);
        if (soup) hulls.push(finish(m.id, soup, i));
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
