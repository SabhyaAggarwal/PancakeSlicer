import { createMachine, useMachine } from '../flow/fsm';

export type SetupStep = 'closed' | 'welcome' | 'tour' | 'printer' | 'profiles' | 'connect' | 'filaments' | 'applying' | 'done';
export const TOUR_PAGES = 4;
interface SetupContext { page: number; error: string | null }
type SetupEvent = { type: 'OPEN' } | { type: 'CLOSE' } | { type: 'NEXT' } | { type: 'BACK' } | { type: 'SKIP' } | { type: 'APPLY' }
  | { type: 'APPLIED' } | { type: 'FAILED'; error: string };

const close = { target: 'closed' } as const;
export const setup = createMachine<SetupStep, SetupEvent, SetupContext>({
  id: 'setup', initial: 'closed', strict: false,
  context: { page: 0, error: null },
  states: {
    closed: { on: { OPEN: { target: 'welcome', action: () => ({ page: 0, error: null }) } } },
    welcome: { on: { CLOSE: close, NEXT: { target: 'tour', action: () => ({ page: 0 }) }, SKIP: 'printer' } },
    tour: { on: { CLOSE: close, SKIP: 'printer',
      NEXT: [{ guard: (c) => c.page < TOUR_PAGES - 1, action: (c) => ({ page: c.page + 1 }) }, { target: 'printer' }],
      BACK: [{ guard: (c) => c.page > 0, action: (c) => ({ page: c.page - 1 }) }, { target: 'welcome' }] } },
    printer: { on: { CLOSE: close, NEXT: 'profiles', BACK: { target: 'tour', action: () => ({ page: TOUR_PAGES - 1 }) } } },
    profiles: { on: { CLOSE: close, NEXT: 'connect', BACK: 'printer' } },
    connect: { on: { CLOSE: close, NEXT: 'filaments', SKIP: 'filaments', BACK: 'profiles' } },
    filaments: { on: { CLOSE: close, APPLY: { target: 'applying', action: () => ({ error: null }) }, BACK: 'connect' } },
    applying: { on: { APPLIED: 'done', FAILED: { target: 'filaments', action: (_, e) => ({ error: e.error }) } } },
    done: { on: { CLOSE: close, BACK: 'filaments' } },
  },
});
export const openSetup = () => setup.send({ type: 'OPEN' });
export const useSetup = () => useMachine(setup);
