import { documentGeneration, onLeaveDocument, subscribeHistory } from '../state/history';
import { assertNever, createMachine } from './fsm';

export type OperationState = 'idle' | 'running' | 'done' | 'failed' | 'canceled';
export type OperationPhase = 'dropped' | 'validating' | 'repairing' | 'simplifying' | 'starting' | 'preparing' | 'saving' | 'downloading' | 'loading' | 'uploading' | 'meshing' | 'decimating' | 'exporting';
export interface OperationContext { request: number; phase: OperationPhase; pct: number; error: string | null }
type OperationEvent =
  | { type: 'START'; request: number; phase: OperationPhase }
  | { type: 'RETRY'; request: number; phase: OperationPhase }
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
export type ImportStage = 'idle' | 'dropped' | 'validating' | 'repairing' | 'simplifying' | 'uploading' | 'ready' | 'failed' | 'canceled';
export function importStage(phase: OperationPhase): Extract<ImportStage, 'dropped' | 'validating' | 'repairing' | 'simplifying' | 'uploading'> {
  switch (phase) {
    case 'starting': case 'dropped': return 'dropped';
    case 'preparing': case 'loading': case 'validating': case 'meshing': return 'validating';
    case 'repairing': return 'repairing';
    case 'decimating': case 'simplifying': return 'simplifying';
    case 'uploading': case 'saving': case 'downloading': case 'exporting': return 'uploading';
    default: return assertNever(phase);
  }
}
export function createOperation(id: string, stages = false) {
  let serial = 0, controller: AbortController | null = null;
  const common = {
    START: { target: stages ? 'dropped' : 'running', action: (_: OperationContext, e: { request: number; phase: OperationPhase }) => ({ request: e.request, phase: e.phase, pct: 0, error: null }) },
    CANCEL: 'canceled',
  } as const;
  const active = ['dropped', 'validating', 'repairing', 'simplifying', 'uploading'] as const;
  const importNode: import('./fsm').StateNode<OperationState | ImportStage, OperationEvent, OperationContext> = { on: { ...common,
        PROGRESS: active.map((target) => ({ target, guard: (_: OperationContext, e: Extract<OperationEvent, { type: 'PROGRESS' }>) => importStage(e.phase) === target,
          action: (_: OperationContext, e: Extract<OperationEvent, { type: 'PROGRESS' }>) => ({ phase: e.phase, pct: e.pct }) })),
        DONE: 'ready', FAIL: { target: 'failed', action: (_: OperationContext, e: { error: string }) => ({ error: e.error }) },
      } };
  const lifecycle = createMachine<OperationState | ImportStage, OperationEvent, OperationContext>({
    id, initial: 'idle', context: { request: 0, phase: 'starting', pct: 0, error: null },
    states: {
      idle: { on: common },
      running: { on: { ...common, PROGRESS: { action: (_, e) => ({ pct: e.pct, phase: e.phase }) },
        DONE: 'done', FAIL: { target: 'failed', action: (_, e) => ({ error: e.error }) } } },
      dropped: importNode, validating: importNode, repairing: importNode, simplifying: importNode, uploading: importNode,
      ready: { on: common }, done: { on: common },
      failed: { on: { ...common, RETRY: common.START } }, canceled: { on: { ...common, RETRY: common.START } },
    },
  });
  const project = (s: ReturnType<typeof lifecycle.get>): import('./fsm').Snapshot<OperationState, OperationContext> => ({ ...s,
    value: (active as readonly string[]).includes(s.value) ? 'running' : s.value === 'ready' ? 'done' : s.value as OperationState });
  let projected = project(lifecycle.get());
  lifecycle.subscribe((s) => { projected = project(s); });
  const machine = { def: lifecycle.def, get: () => projected, send: lifecycle.send, can: lifecycle.can,
    subscribe: (l: (s: typeof projected) => void) => lifecycle.subscribe(() => l(projected)) };
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
  return { machine, lifecycle, cancel, begin(phase: OperationPhase = 'starting'): OperationTicket {
    cancel(); const request = serial, ctl = controller = new AbortController();
    let gen = documentGeneration();
    machine.send({ type: machine.get().value === 'failed' || machine.get().value === 'canceled' ? 'RETRY' : 'START', request, phase });
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
