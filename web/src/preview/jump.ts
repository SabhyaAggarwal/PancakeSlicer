import { setState } from '../state/store';

interface StreamLayers { layers: { first: number }[]; layerEnd: number[] }
let stream: { key: string; data: StreamLayers } | null = null;
export const setLoadedStream = (s: typeof stream) => { stream = s; };

export function layerOfMove(data: StreamLayers, move: number): number | null {
  const i = data.layers.findIndex((l, k) => move >= l.first && move < data.layerEnd[k]);
  return i < 0 ? null : i;
}

export const streamLayerOf = (jobId: string | undefined, move: number) =>
  stream?.key === `job:${jobId}` && stream.data.layers.length ? layerOfMove(stream.data, move) : null;

let pending: number | null = null;
let onJump: (() => void) | null = null;
export function jumpToMove(move: number) {
  pending = move;
  setState({ tab: 'preview' });
  onJump?.();
}
export const takeJump = () => { const m = pending; pending = null; return m; };
export const peekJump = () => pending;
export const setJumpListener = (l: typeof onJump) => { onJump = l; };

let pendingFile: File | null = null;
let onFile: ((f: File) => void) | null = null;
export function openGcode() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.gcode,.gco,.g,.3mf';
  input.onchange = () => {
    const f = input.files?.[0];
    if (!f) return;
    setState({ tab: 'preview' });
    if (onFile) onFile(f); else pendingFile = f;
  };
  input.click();
}
export const takeGcode = () => { const f = pendingFile; pendingFile = null; return f; };
export const setGcodeListener = (l: typeof onFile) => { onFile = l; };
