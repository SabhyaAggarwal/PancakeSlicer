import { getState, setState, type HeadInfo } from './state/store';

export interface HeadBox {
  name?: string;
  min: [number, number, number];
  max: [number, number, number];
  infinite_x?: boolean;
  infinite_y?: boolean;
  nozzle?: boolean;
}

export interface Head extends HeadInfo {
  provenance?: string;
  margin: number;
  lateral_margin: number;
  tip_allowance: { radius: number; height: number };
  boxes: HeadBox[];
}

let index: Promise<HeadInfo[]> | null = null;
const heads = new Map<string, Promise<Head | null>>();

export function loadHeadIndex(): Promise<HeadInfo[]> {
  index ??= fetch('/nonplanar/heads/index.json')
    .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`${r.status} ${r.statusText}`))))
    .then((list: HeadInfo[]) => {
      setState({ heads: list });
      return list;
    })
    .catch((e) => {
      index = null;
      throw e;
    });
  return index;
}

export function loadHead(id: string): Promise<Head | null> {
  if (!/^[a-z0-9_-]+$/.test(id)) return Promise.resolve(null);
  let head = heads.get(id);
  if (!head) {
    head = fetch(`/nonplanar/heads/${id}.json`).then((r) => (r.ok ? r.json() : null)).then((h) => h && {
      id: String(h.id ?? id), name: String(h.name ?? id), calibrated: h.calibrated === true, provenance: h.provenance,
      margin: Number(h.margin ?? 0.3), lateral_margin: Number(h.lateral_margin ?? 0.3),
      tip_allowance: { radius: Number(h.nozzle?.allowance_radius ?? 0), height: Number(h.nozzle?.allowance_height ?? 0) }, boxes: h.boxes ?? [],
    }).catch(() => null);
    heads.set(id, head);
  }
  return head;
}

export const headInfo = (id: string): HeadInfo | undefined => getState().heads?.find((h) => h.id === id);

export const headLabel = (id: string) =>
  id ? `${id} ${headInfo(id)?.calibrated ? '(calibrated)' : '(uncalibrated)'}` : '(none: generic conservative head)';

export const headShortName = (h: HeadInfo) => h.name.replace(/\s*\(.*$/, '') || h.id;

export function maxSlope(head: Head, phi: number, gap: number): number {
  const u = [Math.cos(phi), Math.sin(phi)];
  let tanMin = Infinity;
  for (const box of head.boxes) {
    if (box.infinite_x || box.infinite_y) continue;
    const grow = box.nozzle ? 0 : head.lateral_margin;
    const lo = [box.min[0] - grow, box.min[1] - grow], hi = [box.max[0] + grow, box.max[1] + grow];
    let pmin = Infinity, pmax = -Infinity;
    for (const c of [lo, hi, [lo[0], hi[1]], [hi[0], lo[1]]]) {
      const d = c[0] * u[0] + c[1] * u[1];
      pmin = Math.min(pmin, d);
      pmax = Math.max(pmax, d);
    }
    if (pmax <= 0) continue;
    const num = gap + box.min[2] - (box.nozzle ? 0 : head.margin);
    const limit = num >= 0 ? num / pmax : pmin > 0 ? num / pmin : -Infinity;
    tanMin = Math.min(tanMin, limit);
  }
  return tanMin === Infinity ? Math.PI / 2 : Math.atan(Math.max(0, tanMin));
}

export const slopeMap = (head: Head, n: number, gap: number) =>
  Array.from({ length: n }, (_, i) => maxSlope(head, (2 * Math.PI * i) / n, gap));

export const slopeLimit = (head: Head, gap: number) => (Math.min(...slopeMap(head, 360, gap)) * 180) / Math.PI;
