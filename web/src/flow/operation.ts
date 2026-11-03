import { documentGeneration, onLeaveDocument, subscribeHistory } from '../state/history';
import { createMachine } from './fsm';

export type OperationState = 'idle' | 'running' | 'done' | 'failed' | 'canceled';
export type OperationPhase = 'starting' | 'preparing' | 'saving' | 'downloading' | 'loading' | 'uploading' | 'meshing' | 'decimating' | 'exporting';
export interface OperationContext { request: number; phase: OperationPhase; pct: number; error: string | null }
type OperationEvent =
  | { type: 'START'; request: number; phase: OperationPhase }
  | { type: 'PROGRESS'; pct: number; phase: OperationPhase }
  | { type: 'DONE' } | { type: 'FAIL'; error: string } | { type: 'CANCEL' };
export interface OperationTicket {
  readonly signal: AbortSignal;
  current(): boolean;
  progress(pct: number, phase?: OperationPhase): void;
  done(): void;
  fail(error: unknown): void;
  adoptGeneration(): void;
}
export function createOperation(id: string) {
  let serial = 0, controller: AbortController | null = null;
  const common = {
    START: { target: 'running', action: (_: OperationContext, e: { request: number; phase: OperationPhase }) => ({ request: e.request, phase: e.phase, pct: 0, error: null }) },
    CANCEL: 'canceled',
  } as const;
  const machine = createMachine<OperationState, OperationEvent, OperationContext>({
    id, initial: 'idle', context: { request: 0, phase: 'starting', pct: 0, error: null },
    states: {
      idle: { on: common },
      running: { on: { ...common, PROGRESS: { action: (_, e) => ({ pct: e.pct, phase: e.phase }) },
        DONE: 'done', FAIL: { target: 'failed', action: (_, e) => ({ error: e.error }) } } },
      done: { on: common }, failed: { on: common }, canceled: { on: common },
    },
  });
  const cancel = () => {
    ++serial; controller?.abort(); controller = null;
    if (machine.get().value === 'running') machine.send({ type: 'CANCEL' });
  };
  onLeaveDocument(cancel);
  let generation = documentGeneration();
  subscribeHistory(() => {
    if (generation !== documentGeneration()) {
      generation = documentGeneration();
      if (machine.get().value === 'running') cancel();
    }
  });
  window.addEventListener('popstate', cancel);
  window.addEventListener('hashchange', cancel);
  window.addEventListener('keydown', (e) => { if (e.key === 'Escape') cancel(); });
  return { machine, cancel, begin(phase: OperationPhase = 'starting'): OperationTicket {
    cancel(); const request = serial, ctl = controller = new AbortController();
    let gen = documentGeneration();
    machine.send({ type: 'START', request, phase });
    const current = () => request === serial && gen === documentGeneration() && !ctl.signal.aborted;
    return {
      signal: ctl.signal, current,
      progress: (pct, next = phase) => { if (current()) machine.send({ type: 'PROGRESS', pct, phase: next }); },
      done: () => { if (current()) machine.send({ type: 'DONE' }); },
      fail: (e) => { if (current()) machine.send({ type: 'FAIL', error: e instanceof Error ? e.message : String(e) }); },
      adoptGeneration: () => { gen = documentGeneration(); },
    };
  } };
}
