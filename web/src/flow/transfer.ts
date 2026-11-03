import { createMachine } from './fsm';
export type Transfer = { state: 'uploading' | 'starting' | 'done' | 'failed'; sent: number; total: number; message?: string; error?: string };
type TransferState = 'idle' | 'submitting' | 'uploading' | 'starting' | 'sent' | 'failed';
type TransferEvent = { type: 'START' } | { type: 'STATUS'; transfer: Transfer } | { type: 'FAIL'; error: string } | { type: 'RESET' };
interface TransferContext { transfer: Transfer | null; error: string }
const reset = { target: 'idle', action: () => ({ transfer: null, error: '' }) } as const;
const start = { target: 'submitting', action: () => ({ transfer: null, error: '' }) } as const;
const status = [
  { target: 'uploading', guard: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => e.transfer.state === 'uploading', action: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => ({ transfer: e.transfer }) },
  { target: 'starting', guard: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => e.transfer.state === 'starting', action: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => ({ transfer: e.transfer }) },
  { target: 'sent', guard: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => e.transfer.state === 'done', action: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => ({ transfer: e.transfer }) },
  { target: 'failed', action: (_: TransferContext, e: Extract<TransferEvent, { type: 'STATUS' }>) => ({ transfer: e.transfer, error: e.transfer.error ?? e.transfer.message ?? 'Send failed' }) },
] as const;
const running = { RESET: reset, STATUS: [...status], FAIL: { target: 'failed' as const, action: (_: TransferContext, e: { error: string }) => ({ error: e.error }) } };
export const createTransfer = () => createMachine<TransferState, TransferEvent, TransferContext>({
  id: 'transfer', initial: 'idle', context: { transfer: null, error: '' },
  states: {
    idle: { on: { START: start, RESET: reset } },
    submitting: { on: running }, uploading: { on: running }, starting: { on: running },
    sent: { on: { START: start, RESET: reset } }, failed: { on: { START: start, RESET: reset } },
  },
});
