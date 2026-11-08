import { analyze, type AnalyzeIn } from './analyze';

export type PreflightIn = AnalyzeIn & { id: number };
export interface PreflightOut {
  id: number;
  height: number;
  contact: number;
  overhangs: { tris: Uint32Array; area: number; reach: number; bridge: boolean }[];
}

self.onmessage = (e: MessageEvent<PreflightIn>) => {
  const { id, ...inp } = e.data;
  const a = analyze(inp);
  const out: PreflightOut = { id, height: a.height, contact: a.contact, overhangs: a.overhangs.map(({ tris, area, reach, bridge }) => ({ tris, area, reach, bridge })) };
  (self as unknown as Worker).postMessage(out, out.overhangs.map((r) => r.tris.buffer));
};
