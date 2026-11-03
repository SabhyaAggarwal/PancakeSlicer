import { createMachine } from './fsm';
type PlaybackEvent = { type: 'TOGGLE' } | { type: 'PAUSE' } | { type: 'END' } | { type: 'RATE'; rate: number };
const common = { PAUSE: 'paused', RATE: { action: (_: { rate: number }, e: { rate: number }) => ({ rate: e.rate }) } } as const;
export const createPlayback = () => createMachine<'paused' | 'playing' | 'ended', PlaybackEvent, { rate: number }>({
  id: 'playback', initial: 'paused', context: { rate: 60 },
  states: {
    paused: { on: { ...common, TOGGLE: 'playing' } },
    playing: { on: { ...common, TOGGLE: 'paused', END: 'ended' } },
    ended: { on: { ...common, TOGGLE: 'playing' } },
  },
});
