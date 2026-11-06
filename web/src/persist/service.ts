import { mutation, responseError } from '../api/access';
import { documentText, parseDocument } from './wire';
import { readHealth } from '../api/health';


export async function serviceRequest(path: string, init: RequestInit = {}, missing = false): Promise<Response | null> {
  try {
    const write = init.method && init.method !== 'GET' && init.method !== 'HEAD';

    const request = { cache: write ? 'no-store' as const : 'no-cache' as const, ...init, signal: init.signal ?? AbortSignal.timeout(30_000) };
    const response = write ? await mutation(path, request) : await fetch(path, request);
    if (missing && response.status === 404) return null;
    if (!response.ok) throw await responseError(response);
    return response;
  } catch (error) {
    const status = (error as { status?: number })?.status;
    if (missing && status === 404) return null;
    const name = status === 409 ? 'ProjectConflictError' : status === 410 ? 'ProjectDeletedError'
      : status === 507 || error instanceof Error && error.message === 'Server storage full' ? 'QuotaExceededError' : 'ProjectServiceError';
    throw Object.assign(error instanceof Error ? error : new Error(String(error)), { name });
  }
}
export async function serviceJson<T>(path: string, init?: RequestInit, missing = false): Promise<T | null> {
  const response = await serviceRequest(path, init, missing);
  return response && parseDocument<T>(await response.text());
}
export function jsonWrite(method: 'PUT' | 'POST' | 'PATCH' | 'DELETE', value: unknown): RequestInit {
  return { method, headers: { 'Content-Type': 'application/json' }, body: documentText(value) };
}
export const routeKey = (key: string) => encodeURIComponent(key);

let workspaceService: Promise<void> | null = null;

export function requireWorkspaceService(): Promise<void> {
  workspaceService ??= readHealth(15_000).then(async response => {

    if (!response.ok) throw Object.assign(new Error('The slicing service is unavailable.'), { status: response.status });
    if ((await response.json()).workspace_schema !== 1)
      throw new Error('The updated storage service is required. Your projects, drafts and preference changes are kept; retry after the service update.');
  }).catch(error => { workspaceService = null; throw error; });
  return workspaceService;
}
