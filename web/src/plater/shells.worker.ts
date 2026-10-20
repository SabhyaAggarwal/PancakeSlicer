import { printInPlaceGap } from './shells';

export interface ShellsIn { id: number; positions: Float32Array; index: Uint32Array }
export interface ShellsOut { id: number; gap: number | null }

self.onmessage = (e: MessageEvent<ShellsIn>) => {
  const { id, positions, index } = e.data;
  let gap: number | null = null;
  try {
    gap = printInPlaceGap(positions, index);
  } catch {
  }
  (self as unknown as Worker).postMessage({ id, gap } satisfies ShellsOut);
};
