import { mutation, responseError } from '../api/access';
import { assertNever, createMachine, useMachine } from './fsm';
import { readRecovery, rememberRecovery } from './recovery';
export type Transfer = { state: 'uploading' | 'starting' | 'done' | 'failed'; sent: number; total: number; message?: string; error?: string };
export type TransferState = 'idle' | 'connecting' | 'sending' | 'sent' | 'failed';
type TransferEvent = { type: 'START'; jobId: string; target: string } | { type: 'STATUS'; transfer: Transfer }
  | { type: 'FAIL'; error: string } | { type: 'RESET' } | { type: 'CANCEL' } | { type: 'RETRY' };
interface TransferContext { jobId: string | null; target: string | null; transfer: Transfer | null; error: string }
const transferEffects = new WeakMap<object, { cancel(): void; retry(): void; reset(): void }>();
const reset = { target: 'idle', action: (_: TransferContext, _e: TransferEvent, m: object) => {
  transferEffects.get(m)?.reset(); return { jobId: null, target: null, transfer: null, error: '' };
} } as const;
const start = { target: 'connecting', guard: (_: TransferContext, e: Extract<TransferEvent, { type: 'START' }>) => !!e.jobId && !!e.target, action: (_: TransferContext, e: Extract<TransferEvent, { type: 'START' }>) =>
  ({ jobId: e.jobId, target: e.target, transfer: null, error: '' }) } as const;
function transferState(transfer: Transfer): 'sending' | 'sent' | 'failed' {
  const state = transfer.state;
  switch (state) {
    case 'uploading': case 'starting': return 'sending';
    case 'done': return 'sent';
    case 'failed': return 'failed';
    default: return assertNever(state);
  }
}
const status = (['sending', 'sent', 'failed'] as const).map((target) => ({ target,
  guard: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => transferState(e.transfer) === target,
  action: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => ({ transfer: e.transfer,
    error: e.transfer.state === 'failed' ? e.transfer.error ?? e.transfer.message ?? 'Send failed' : '' }),
}));
const common = { RESET: reset, CANCEL: { target: 'failed' as const, action: (_: TransferContext, _e: TransferEvent, m: object) => {
  transferEffects.get(m)?.cancel(); return { error: 'Send interrupted. Reconnect to check the printer before retrying.' };
} } };
const running = { ...common, STATUS: status, FAIL: { target: 'failed' as const, action: (_: TransferContext, e: { error: string }) => ({ error: e.error }) } };
export const createTransfer = () => createMachine<TransferState, TransferEvent, TransferContext>({
  id: 'transfer', initial: 'idle', context: { jobId: null, target: null, transfer: null, error: '' },
  states: {
    idle: { on: { START: start, RESET: reset } },
    connecting: { on: running }, sending: { on: running },
    sent: { on: { START: start, RESET: reset } },
    failed: { on: { START: start, RESET: reset, RETRY: { action: (_: TransferContext, _e: TransferEvent, m: object) => transferEffects.get(m)?.retry() } } },
  },
});
export function createPrinterSend() {
  const machine = createTransfer();
  let ctl: AbortController | null = null, timer: ReturnType<typeof setTimeout> | undefined, epoch = 0;
  const stop = () => { epoch++; ctl?.abort(); ctl = null; clearTimeout(timer); };
  const connect = async (jobId: string, target: string, body?: Record<string, unknown>) => {
    if (!machine.can({ type: 'START', jobId, target })) return;
    stop(); const mine = epoch, controller = ctl = new AbortController();
    machine.send({ type: 'START', jobId, target });
    rememberRecovery({ send: { jobId, target } });
    const poll = async (submit = false) => {
      const timeout = setTimeout(() => controller.abort(), 10_000);
      try {
        const path = `/api/jobs/${encodeURIComponent(jobId)}/send`;
        const r = submit ? await mutation(path, { method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ ...body, printer: target }), signal: controller.signal }) : await fetch(path, { signal: controller.signal });
        if (!r.ok) throw await responseError(r);
        const transfer = await r.json() as Transfer;
        if (mine !== epoch) return;
        machine.send({ type: 'STATUS', transfer });
        if (machine.get().value === 'sending') timer = setTimeout(() => void poll(), 500);
        else if (machine.get().value === 'sent') rememberRecovery({ send: undefined });
      } catch (e) {
        if (mine === epoch) machine.send({ type: 'FAIL', error: e instanceof Error ? e.message : 'Send interrupted. Reconnect to check its status.' });
      } finally { clearTimeout(timeout); }
    };
    await poll(body !== undefined);
  };
  let retryRequest = Promise.resolve();
  transferEffects.set(machine, {
    cancel: stop,
    reset: () => { stop(); rememberRecovery({ send: undefined }); },
    retry: () => {
      const { jobId, target } = machine.get().context;
      if (jobId && target) retryRequest = connect(jobId, target);
    },
  });
  return { machine,
    submit: (printer: string, job: string, options: Record<string, unknown> = {}) => connect(job, printer, options),
    start: (jobId: string, target: string, body: Record<string, unknown>) => connect(jobId, target, body),
    reconnect: (jobId: string, target: string) => connect(jobId, target),
    retry: () => { machine.send({ type: 'RETRY' }); return retryRequest; },
    cancel: () => machine.send({ type: 'CANCEL' }),
    reset: () => machine.send({ type: 'RESET' }),
  };
}
export const printerSend = createPrinterSend();
export function restorePrinterSend() {
  const send = readRecovery().send;
  if (send) void printerSend.reconnect(send.jobId, send.target);
}

export const usePrinterSend = () => useMachine(printerSend.machine);

export type SendSnapshot = { value: 'idle' } | { value: Exclude<TransferState, 'idle'>; jobId: string; target: string; transfer: Transfer | null; error: string };
export function sendSnapshot(machine = printerSend.machine): SendSnapshot {
  const { value, context: c } = machine.get();
  if (value === 'idle') return { value };
  if (!c.jobId || !c.target) throw new Error('Send target invariant');
  return { value, jobId: c.jobId, target: c.target, transfer: c.transfer, error: c.error };
}
