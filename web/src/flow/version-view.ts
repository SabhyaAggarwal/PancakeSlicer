import type { ProjectVersion } from '../projects/versions';
import { setState, type JobView } from '../state/store';
import { createMachine, type StateNode } from './fsm';

export type VersionViewState = 'latest' | 'opening' | 'viewing' | 'loading-slice';
interface VersionViewContext { request: number; target: ProjectVersion | null; shown: ProjectVersion | null }
type VersionViewEvent = { type: 'VIEW'; version: ProjectVersion } | { type: 'SHOWN'; request: number } | { type: 'FAIL'; request: number }
  | { type: 'SLICE'; request: number; job: JobView } | { type: 'SLICE_FAIL'; request: number } | { type: 'LATEST' } | { type: 'RESTORE' };
type Node = StateNode<VersionViewState, VersionViewEvent, VersionViewContext>;

const mine = (c: VersionViewContext, e: { request: number }) => e.request === c.request;
const late = {} as const;
const view = { target: 'opening', action: (c: VersionViewContext, e: Extract<VersionViewEvent, { type: 'VIEW' }>) => ({ request: c.request + 1, target: e.version }) } as const;
const latest = { target: 'latest', action: () => ({ target: null, shown: null }) } as const;
const shown: Node = { on: { VIEW: view, LATEST: latest, RESTORE: latest, SHOWN: late, FAIL: late, SLICE: late, SLICE_FAIL: late } };

export const versionView = createMachine<VersionViewState, VersionViewEvent, VersionViewContext>({
  id: 'version-view', initial: 'latest', strict: true, context: { request: 0, target: null, shown: null },
  states: {
    latest: { on: { VIEW: view, LATEST: {}, SHOWN: late, FAIL: late, SLICE: late, SLICE_FAIL: late } },
    opening: { on: {
      VIEW: view, LATEST: latest,
      SHOWN: [{ target: 'loading-slice', guard: (c, e) => mine(c, e) && !!c.target?.slice, action: (c) => ({ shown: c.target, target: null }) },
        { target: 'viewing', guard: mine, action: (c) => ({ shown: c.target, target: null }) }, late],
      FAIL: [{ target: 'viewing', guard: (c, e) => mine(c, e) && !!c.shown, action: () => ({ target: null }) },
        { target: 'latest', guard: mine, action: () => ({ target: null }) }, late],
      SLICE: late, SLICE_FAIL: late,
    } },
    viewing: shown,
    'loading-slice': { on: { ...shown.on,
      SLICE: [{ target: 'viewing', guard: mine, action: (_, e) => { setState({ job: e.job }); } }, late],
      SLICE_FAIL: [{ target: 'viewing', guard: mine }, late],
    } },
  },
});

export const versionShown = () => versionView.get().context.shown;
export const versionWanted = (request: number) => versionView.get().value === 'opening' && versionView.get().context.request === request;
