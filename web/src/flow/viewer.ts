import { useSyncExternalStore } from 'react';
import { assertNever, createMachine, useMachine, type Machine } from './fsm';
import { playback } from './playback';
import { finishSlicePreview } from './preview-navigation';
export type ViewerSource = { id: string; moves?: number };
export type ViewerState = 'empty' | 'loading' | 'ready' | 'playing' | 'error';
type ViewerEvent = { type: 'LOAD'; source: ViewerSource } | { type: 'READY'; id: string } | { type: 'FAIL'; id: string; error: string }
  | { type: 'PLAY' } | { type: 'PAUSE' } | { type: 'CLEAR' } | { type: 'RETRY' };
type ViewerContext = { source: ViewerSource | null; error: string | null };
const load = { target: 'loading', action: (_: ViewerContext, e: Extract<ViewerEvent, { type: 'LOAD' }>) => ({ source: e.source, error: null }) } as const;
const clear = { target: 'empty', action: () => ({ source: null, error: null }) } as const;
const fail = { target: 'error', guard: (c: ViewerContext, e: Extract<ViewerEvent, { type: 'FAIL' }>) => c.source?.id === e.id,
  action: (_: ViewerContext, e: Extract<ViewerEvent, { type: 'FAIL' }>) => ({ error: e.error }) } as const;
export const createViewer = () => createMachine<ViewerState, ViewerEvent, ViewerContext>({
  id: 'viewer', initial: 'empty', context: { source: null, error: null },
  states: {
    empty: { on: { LOAD: load, CLEAR: clear } },
    loading: { on: { LOAD: load, CLEAR: clear, FAIL: fail,
      READY: { target: 'ready', guard: (c, e) => c.source?.id === e.id, action: (_c, e) => { finishSlicePreview(e.id, playback.get().context.moves); } } } },
    ready: { on: { LOAD: load, CLEAR: clear, FAIL: fail, PLAY: 'playing', PAUSE: {} } },
    playing: { on: { LOAD: load, CLEAR: clear, FAIL: fail, PAUSE: 'ready' } },
    error: { on: { LOAD: load, CLEAR: clear, RETRY: { target: 'loading', action: () => ({ error: null }) } } },
  },
});
export type ViewerSnapshot = { value: 'empty' } | { value: 'loading' | 'ready' | 'playing'; source: ViewerSource }
  | { value: 'error'; source: ViewerSource; error: string };
export function viewerSnapshot(m: Machine<ViewerState, ViewerEvent, ViewerContext>): ViewerSnapshot {
  const { value, context: c } = m.get();
  if (value === 'empty') return { value };
  if (!c.source) throw new Error('Viewer source invariant');
  switch (value) {
    case 'loading': case 'ready': case 'playing': return { value, source: c.source };
    case 'error': return { value, source: c.source, error: c.error ?? 'Preview unavailable' };
    default: return assertNever(value);
  }
}
export const viewer = createViewer();

export const useViewer = () => useMachine(viewer);
export function loadSource(source: ViewerSource | null) {
  playback.send({ type: 'SOURCE', moves: source?.moves ?? 0 });
  viewer.send(source ? { type: 'LOAD', source } : { type: 'CLEAR' });
}
export const viewerReady = (id: string, moves?: number) => {
  if (viewer.get().context.source?.id !== id) return;
  if (moves !== undefined) playback.send({ type: 'SOURCE', moves });
  viewer.send({ type: 'READY', id });
};
export const viewerFirstMoves = (id: string, moves: number) => {
  if (viewer.get().value === 'loading' && viewer.get().context.source?.id === id) finishSlicePreview(id, moves);
};
export const viewerError = (id: string, error: string) => viewer.send({ type: 'FAIL', id, error });
export const playViewer = () => { if (viewer.can({ type: 'PLAY' })) playback.send({ type: 'PLAY' }); };
export const pauseViewer = () => playback.send({ type: 'PAUSE' });
export const scrubViewer = (position: number, layer?: number) => playback.send({ type: 'SCRUB', position, layer });
playback.subscribe(({ value }) => {
  if (value === 'playing') viewer.send({ type: 'PLAY' });
  else if (viewer.get().value === 'playing') viewer.send({ type: 'PAUSE' });
});
viewer.subscribe(({ value }) => {
  if (value !== 'ready' && value !== 'playing' && playback.get().value === 'playing') playback.send({ type: 'PAUSE' });
});

export type ViewerProgress = { id: string; loaded: number; total: number; bytes: number };
let progress: ViewerProgress | null = null, progressAt = 0;
const progressListeners = new Set<() => void>();
export function noteViewerProgress(next: ViewerProgress) {
  const now = performance.now(), edge = progress?.id !== next.id || next.loaded === 0 || next.loaded >= next.total;
  if (!edge && now - progressAt < 100) return;
  progressAt = now; progress = next;
  progressListeners.forEach((f) => f());
}
export const useViewerProgress = () => useSyncExternalStore((f) => { progressListeners.add(f); return () => { progressListeners.delete(f); }; }, () => progress);
