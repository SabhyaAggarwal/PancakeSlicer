import type { JobRequest } from '../api/types';
import { project } from './project';
import { randomId } from '../random-id';
const keys = new WeakMap<JobRequest, string>();
export function submissionHeaders(request: JobRequest): Record<string, string> {
  let key = keys.get(request);
  if (!key) { key = randomId(); keys.set(request, key); }
  const plain = request.plate && !request.calib && !request.floating && !request.priority && request.preview_budget === undefined;
  const id = plain ? project.get().context.id : '';
  return id ? { 'Idempotency-Key': key, 'X-Slice-Group': `${id}:${request.plate_index ?? 0}` } : { 'Idempotency-Key': key };
}
