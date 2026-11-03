import type { JobRequest } from '../api/types';
const keys = new WeakMap<JobRequest, string>();
export function submissionHeaders(request: JobRequest): Record<string, string> {
  let key = keys.get(request);
  if (!key) { key = crypto.randomUUID(); keys.set(request, key); }
  return { 'Idempotency-Key': key };
}
