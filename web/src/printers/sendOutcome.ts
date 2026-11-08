import { getState, setState, subscribeStore, toast } from '../state/store';
import type { Transfer } from '../flow/transfer';

export type SendTone = 'progress' | 'success' | 'waiting' | 'error';
export interface SendOutcome { tone: SendTone; title: string; detail: string }

const sentence = (s: string) => s.charAt(0).toUpperCase() + s.slice(1);
const mb = (n: number) => (n / 1e6).toFixed(1);

export function sendOutcome(t: Transfer): SendOutcome {
  if (t.state === 'uploading') return { tone: 'progress', title: `Sending to ${t.name}`, detail: `${mb(t.sent)} of ${mb(t.total)} MB (${Math.floor((100 * t.sent) / Math.max(1, t.total))}%)` };
  if (t.state === 'starting') return { tone: 'progress', title: `Sending to ${t.name}`, detail: 'Uploaded; waiting for the printer to accept the start…' };
  if (t.state === 'cancelled') return { tone: 'error', title: 'Send cancelled', detail: sentence(t.error ?? '') };
  switch (t.outcome) {
    case 'printing': return { tone: 'success', title: `Printing on ${t.name}`, detail: '' };
    case 'uploaded': return { tone: 'success', title: `Uploaded to ${t.name}, not started`, detail: 'Start it from the printer\'s screen.' };
    case 'waiting_for_printer': return { tone: 'waiting', title: 'Sent, waiting for the printer to start', detail: `${t.name} has not confirmed the start yet. Check its screen before sending again.` };
    case 'failed': return { tone: 'error', title: `Not sent to ${t.name}`, detail: sentence(t.error ?? '') };
    case undefined: throw new Error(`Send ${t.id} finished without an outcome`);
  }
}

const OUTCOME_LABEL = { printing: 'Started', uploaded: 'Uploaded', waiting_for_printer: 'Not confirmed', failed: 'Send failed' } as const;
const END_LABEL = { finished: 'Finished', failed: 'Failed', cancelled: 'Cancelled' } as const;
export const outcomeLabel = (t: Transfer, running = false) => t.job_end ? END_LABEL[t.job_end] : running ? 'Printing'
  : t.state === 'cancelled' ? 'Cancelled' : t.outcome ? OUTCOME_LABEL[t.outcome] : 'Sending';
export const outcomeTone = (t: Transfer, running = false) => t.job_end === 'failed' || t.outcome === 'failed' ? 'error' : running && !t.job_end ? 'printing' : undefined;

export const openDevice = () => setState({ tab: 'device', appDialog: null });

const SHORT_REASON: Record<string, string> = { credentials: 'check the access code', developer_mode: 'turn on Developer Mode', busy: 'the printer is busy',
  storage: 'check the SD card', timeout: 'the printer did not answer', connection: 'cannot reach the printer', interrupted: 'the service restarted' };

export function noticeSend(t: Transfer) {
  let last = '';
  const show = (x: Transfer) => {
    const key = `${x.state}:${x.outcome}`;
    if (key === last) return;
    last = key;
    if (x.state === 'done' && x.outcome === 'printing') toast(`Printing on ${x.name}`, { label: 'View', run: openDevice }, 8000, 'success', true);
    else if (x.state === 'done' && x.outcome === 'uploaded') toast(`Uploaded to ${x.name}`, { label: 'View', run: openDevice }, 6000, 'success', true);
    else if (x.state === 'done' && x.outcome === 'waiting_for_printer') toast(`Sent to ${x.name}, waiting for it to start`, { label: 'View', run: openDevice }, 8000, 'warning');
    else if (x.state === 'failed' || x.state === 'cancelled') toast(x.state === 'cancelled' ? 'Send cancelled' : `Not sent to ${x.name}${SHORT_REASON[x.reason ?? ''] ? `: ${SHORT_REASON[x.reason!]}` : ''}`, { label: 'Retry', run: () => setState({ appDialog: 'send' }) }, 10000, 'error');
  };
  const settled = (x: Transfer) => x.state !== 'uploading' && x.state !== 'starting' && x.outcome !== 'waiting_for_printer';
  show(t);
  if (settled(t)) return;
  const stop = subscribeStore(() => {
    const x = getState().printerConnections.transfers.find((y) => y.id === t.id);
    if (!x) return;
    show(x);
    if (settled(x)) { stop(); clearTimeout(timer); }
  });
  const timer = setTimeout(stop, 10 * 60_000);
}
