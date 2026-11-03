import { plateSignature } from '../slice-input';
import { documentGeneration, onLeaveDocument } from '../state/history';
import { getState, setState, subscribeStore } from '../state/store';
import { project } from './project';
interface SliceNavigation { token: number; project: string; generation: number; plate: number; signature: string; job?: string }
let serial = 0, pending: SliceNavigation | null = null;
export function beginSlicePreview(signature: string): number {
  const s = getState();
  pending = {token: ++serial, project: project.get().context.id, generation: documentGeneration(), plate: s.plateIndex, signature};
  return serial;
}
export function bindSlicePreview(token: number, id?: string) { if (pending?.token === token && id) pending.job = id; }
export function finishSlicePreview(source: string, moves: number) {
  const intent = pending, s = getState();
  if (!intent || !intent.job || moves < 1 || (source !== `job:${intent.job}` && source !== `job:${intent.job}:extended`)) return;
  pending = null;
  if (s.tab !== 'prepare' || s.job.state !== 'done' || s.job.id !== intent.job || s.job.stale ||
      project.get().context.id !== intent.project || documentGeneration() !== intent.generation || s.plateIndex !== intent.plate ||
      s.job.plateSignature !== intent.signature || plateSignature(s) !== intent.signature) return;
  setState({tab: 'preview'});
}
subscribeStore(() => { const s = getState(); if (s.tab !== 'prepare' || s.job.state === 'failed' || s.job.state === 'canceled') pending = null; });
onLeaveDocument(() => { pending = null; });
