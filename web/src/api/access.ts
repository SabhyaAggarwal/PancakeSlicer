import { sessionId } from './session';
let token = '';
export type AccessNotice = { status: number; message: string };
let notice: AccessNotice | null = null;
const listeners = new Set<() => void>();
export const accessSnapshot = () => notice;
export const subscribeAccess = (listener: () => void) => {
  listeners.add(listener);
  return () => { listeners.delete(listener); };
};
export function accessNotice(value: AccessNotice | null) {
  notice = value;
  listeners.forEach((listener) => listener());
}
export function saveAccessToken(value: string) {
  token = value.trim();
}
export function bootstrapAccessToken() {
  const url = new URL(location.href);
  if (url.searchParams.has('token')) {
    saveAccessToken(url.searchParams.get('token') ?? '');
    url.searchParams.delete('token');
    history.replaceState(history.state, '', url.pathname + url.search + url.hash);
  }
}

function sendWithProgress(path: string, init: RequestInit, headers: Headers, onUpload: (fraction: number) => void): Promise<Response> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open(init.method ?? 'GET', path);
    xhr.responseType = 'blob';
    headers.forEach((value, key) => xhr.setRequestHeader(key, value));
    xhr.upload.onprogress = (e) => e.lengthComputable && onUpload(e.loaded / e.total);
    xhr.onload = () => {
      const headers = new Headers();
      for (const line of xhr.getAllResponseHeaders().trim().split(/\r?\n/)) {
        const at = line.indexOf(':');
        if (at > 0) headers.set(line.slice(0, at).trim(), line.slice(at + 1).trim());
      }
      resolve(new Response(xhr.response, { status: xhr.status, statusText: xhr.statusText, headers }));
    };
    xhr.onerror = xhr.onabort = () => reject(new TypeError('Failed to fetch'));
    xhr.send(init.body as XMLHttpRequestBodyInit);
  });
}

const GZIP_MIN = 16 << 10;
async function gzipBody(init: RequestInit): Promise<RequestInit> {
  const body = typeof init.body === 'string' ? new Blob([init.body]) : init.body instanceof Blob ? init.body : null;
  if (!body || body.size < GZIP_MIN || typeof CompressionStream === 'undefined') return init;
  const magic = new TextDecoder('latin1').decode(await body.slice(0, 5).arrayBuffer());
  if (magic.startsWith('PK\x03\x04') || magic === 'DRACO') return init;
  const headers = new Headers(init.headers);
  headers.set('Content-Encoding', 'gzip');
  return { ...init, headers, body: await new Response(body.stream().pipeThrough(new CompressionStream('gzip'))).blob() };
}

const showNotice = (value: AccessNotice | null) => accessNotice(value);

let discarding = false;
export const discardPendingWrites = (on = true) => { discarding = on; };
export const writesDiscarded = () => discarding;
export async function mutation(path: string, init: RequestInit, retry = false, signal?: AbortSignal, onUpload?: (fraction: number) => void, quiet = false): Promise<Response> {
  if (discarding) throw new Error('Starting over: this page\'s changes are discarded.');
  const accessNotice = quiet ? () => undefined : showNotice;
  init = await gzipBody(init);
  const send = () => {
    const headers = new Headers(init.headers);
    if (token) headers.set('Authorization', `Bearer ${token}`);
    headers.set('X-Orca-Session', sessionId);
    return onUpload && import.meta.env.MODE !== 'demo' ? sendWithProgress(path, init, headers, onUpload) : fetch(path, { ...init, headers });
  };
  let response = await send();
  if ((response.status === 429 || response.status === 503) && retry) {
    const raw = response.headers.get('Retry-After') ?? '1';
    const seconds = /^\d+$/.test(raw) ? Math.min(86400, Number(raw)) : 1;
    const current = { status: response.status, message: `Server busy. Retry after ${seconds} seconds. Retrying once automatically.` };
    accessNotice(current);
    try {
      await new Promise<void>((resolve, reject) => {
        const abort = () => {
          clearTimeout(timer);
          reject(new Error('Slicing Canceled'));
        };
        const timer = setTimeout(() => {
          signal?.removeEventListener('abort', abort);
          resolve();
        }, seconds * 1000);
        if (signal?.aborted) abort();
        else signal?.addEventListener('abort', abort, { once: true });
      });
    } finally {
      if (notice === current) accessNotice(null);
    }
    if (signal?.aborted) throw new Error('Slicing Canceled');
    response = await send();
  }
  if (response.status === 401) {
    accessNotice({ status: 401, message: 'Access token required' });
    throw new Error('Access token required');
  }
  if (response.status === 507) {
    accessNotice({ status: 507, message: 'Server storage full' });
    throw new Error('Server storage full');
  }
  if (response.status === 429 || response.status === 503) {
    const raw = response.headers.get('Retry-After') ?? '1';
    const seconds = /^\d+$/.test(raw) ? Math.min(86400, Number(raw)) : 1;
    const message = `Server busy. Retry after ${seconds} seconds.`;
    accessNotice({ status: response.status, message });
    throw Object.assign(new Error(message), { status: response.status });
  }
  if (!response.ok) {
    const error = await responseError(response);
    if (error.code !== 'revision_conflict' && error.code !== 'draft_conflict' && error.code !== 'blob_missing' && error.code !== 'project_not_archived') accessNotice({ status: response.status, message: error.message });
    throw error;
  }
  return response;
}

export interface ApiError extends Error { status:number; code?:string; recovery?:string; profile?:{kind:string;name:string;vendor:string;slot?:number}; revision?:number; missing?:string[]; document?:Record<string,unknown> }
export async function responseError(response: Response): Promise<ApiError> {
  const text = await response.text();
  let message = `${response.status} ${response.statusText}: ${text.slice(0, 300)}`;
  const details: Partial<ApiError> = {};
  try {
    const j = JSON.parse(text);
    if (typeof j?.error === 'string' && j.error) message = j.error;
    if(typeof j.code === 'string')details.code=j.code;
    if(typeof j.recovery === 'string')details.recovery=j.recovery;
    if(Number.isInteger(j.revision))details.revision=j.revision;
    if(Array.isArray(j.missing))details.missing=j.missing;
    if(j.document && typeof j.document === 'object' && !Array.isArray(j.document))details.document=j.document;
    if(j.profile && typeof j.profile.kind === 'string' && typeof j.profile.name === 'string')details.profile=j.profile;
  } catch {  }
  return Object.assign(new Error(message), { status: response.status },details);
}
