const TOKEN_KEY = 'orca.web.bearerToken';
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
  try {
    if (token) localStorage.setItem(TOKEN_KEY, token);
    else localStorage.removeItem(TOKEN_KEY);
  } catch {  }
}
export function bootstrapAccessToken() {
  const url = new URL(location.href);
  if (url.searchParams.has('token')) {
    saveAccessToken(url.searchParams.get('token') ?? '');
    url.searchParams.delete('token');
    history.replaceState(history.state, '', url.pathname + url.search + url.hash);
  } else {
    try { token = localStorage.getItem(TOKEN_KEY) ?? ''; } catch { token = ''; }
  }
}

export async function mutation(path: string, init: RequestInit, retry = false, signal?: AbortSignal): Promise<Response> {
  const send = () => {
    const headers = new Headers(init.headers);
    if (token) headers.set('Authorization', `Bearer ${token}`);
    return fetch(path, { ...init, headers });
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
    throw new Error(message);
  }
  if (!response.ok) {
    const error = await responseError(response);
    accessNotice({ status: response.status, message: error.message });
    throw error;
  }
  return response;
}

export async function responseError(response: Response): Promise<Error> {
  const text = await response.text();
  try {
    const j = JSON.parse(text);
    if (typeof j?.error === 'string' && j.error) return new Error(j.error);
  } catch {  }
  return new Error(`${response.status} ${response.statusText}: ${text.slice(0, 300)}`);
}
