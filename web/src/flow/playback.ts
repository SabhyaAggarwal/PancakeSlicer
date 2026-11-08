import { createMachine, useMachine } from './fsm';
type PlaybackEvent = { type: 'TOGGLE' } | { type: 'PLAY' } | { type: 'PAUSE' } | { type: 'END' }
  | { type: 'RATE'; rate: number } | { type: 'SOURCE'; moves: number }
  | { type: 'SCRUB'; position: number; layer?: number } | { type: 'POSITION'; position: number; layer: number };
interface PlaybackContext { rate: number; position: number; layer: number; moves: number }
const position = (c: PlaybackContext, e: { position: number; layer?: number }) => ({
  position: Math.max(0, Math.min(c.moves - 1, Math.floor(Number.isFinite(e.position) ? e.position : c.position))), layer: e.layer ?? c.layer,
});
const common = {
  PAUSE: 'paused', RATE: { action: (_: PlaybackContext, e: { rate: number }) => ({ rate: Math.max(1, Number.isFinite(e.rate) ? e.rate : 60) }) },
  SOURCE: { target: 'paused', action: (_: PlaybackContext, e: { moves: number }) => ({ moves: Math.max(0, Math.floor(e.moves)), position: 0, layer: 0 }) },
  SCRUB: { target: 'paused', action: position }, POSITION: { action: position },
} as const;
const play = { target: 'playing', guard: (c: PlaybackContext) => c.moves > 0 } as const;
export const createPlayback = () => createMachine<'paused' | 'playing' | 'ended', PlaybackEvent, PlaybackContext>({
  id: 'playback', initial: 'paused', context: { rate: 60, position: 0, layer: 0, moves: 0 },
  states: {
    paused: { on: { ...common, TOGGLE: play, PLAY: play } },
    playing: { on: { ...common, TOGGLE: 'paused', PLAY: {}, END: 'ended' } },
    ended: { on: { ...common, TOGGLE: { ...play, action: () => ({ position: 0, layer: 0 }) }, PLAY: { ...play, action: () => ({ position: 0, layer: 0 }) } } },
  },
});
export const playback = createPlayback();
export const playbackRange = () => [0, Math.max(0, playback.get().context.moves - 1)] as const;
export const usePlayback = () => useMachine(playback);
if (typeof window !== 'undefined') {
  window.addEventListener('keydown', (e) => { if (e.key === 'Escape') playback.send({ type: 'PAUSE' }); });
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') playback.send({ type: 'PAUSE' }); });
}
