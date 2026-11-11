import { createMachine, useMachine } from '../flow/fsm';
import { TOUR } from './tour';

export type SetupStep = 'closed' | 'welcome' | 'tour' | 'printer' | 'connect' | 'filaments' | 'applying';
export const TOUR_PAGES = TOUR.length;
interface SetupContext { page: number; error: string | null }
type SetupEvent = { type: 'OPEN'; at?: 'printer' } | { type: 'CLOSE' } | { type: 'NEXT' } | { type: 'BACK' } | { type: 'SKIP' } | { type: 'APPLY' }
  | { type: 'APPLIED' } | { type: 'FAILED'; error: string };

const close = { target: 'closed' } as const;
export const setup = createMachine<SetupStep, SetupEvent, SetupContext>({
  id: 'setup', initial: 'closed', strict: false,
  context: { page: 0, error: null },
  states: {
    closed: { on: { OPEN: [{ guard: (_, e) => e.type === 'OPEN' && e.at === 'printer', target: 'printer', action: () => ({ page: 0, error: null }) }, { target: 'welcome', action: () => ({ page: 0, error: null }) }] } },
    welcome: { on: { CLOSE: close, NEXT: [{ guard: () => TOUR_PAGES > 0, target: 'tour', action: () => ({ page: 0 }) }, { target: 'printer' }], SKIP: 'printer' } },
    tour: { on: { CLOSE: close, SKIP: 'printer',
      NEXT: [{ guard: (c) => c.page < TOUR_PAGES - 1, action: (c) => ({ page: c.page + 1 }) }, { target: 'printer' }],
      BACK: [{ guard: (c) => c.page > 0, action: (c) => ({ page: c.page - 1 }) }, { target: 'welcome' }] } },
    printer: { on: { CLOSE: close, NEXT: 'connect', BACK: [{ guard: () => TOUR_PAGES > 0, target: 'tour', action: () => ({ page: TOUR_PAGES - 1 }) }, { target: 'welcome' }] } },
    connect: { on: { CLOSE: close, NEXT: 'filaments', BACK: 'printer' } },
    filaments: { on: { CLOSE: close, APPLY: { target: 'applying', action: () => ({ error: null }) }, BACK: 'connect' } },
    applying: { on: { APPLIED: close, FAILED: { target: 'filaments', action: (_, e) => ({ error: e.error }) } } },
  },
});
export const openSetup = (at?: 'printer') => setup.send({ type: 'OPEN', at });
export const useSetup = () => useMachine(setup);
