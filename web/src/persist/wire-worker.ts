import type { WireRequest, WireResponse } from './wire-protocol';

type Pending = { resolve(value: WireResponse): void; reject(error: Error): void; timer: ReturnType<typeof setTimeout> };
type Connection = { worker: Worker; pending: Map<number, Pending>; failed: boolean; fail(message: string): void };
let connection: Connection | null = null, serial = 0;
const unavailable = (message: string) => Object.assign(new Error(`${message} Keep this tab open and try again.`), { name: 'ProjectWireWorkerError' });
function getConnection(): Connection {
  if (connection) return connection;
  let worker: Worker;
  try { worker = new Worker(new URL('./wire.worker.ts', import.meta.url), { type: 'module' }); }
  catch { throw unavailable('Oops! The background project saver couldn\'t start.'); }
  const current: Connection = { worker, pending: new Map(), failed: false, fail: (message) => fail(message) };
  const fail = (message: string) => {
    if (current.failed) return;
    current.failed = true; worker.terminate();
    if (connection === current) connection = null;
    for (const pending of current.pending.values()) { clearTimeout(pending.timer); pending.reject(unavailable(message)); }
    current.pending.clear();
  };
  worker.addEventListener('error', (event) => { event.preventDefault(); fail('Oops! The background project saver ran into an error.'); });
  worker.addEventListener('messageerror', () => fail('Project wire worker response is unreadable.'));
  worker.addEventListener('message', (event: MessageEvent<WireResponse>) => {
    const response = event.data;
    if (!response || !Number.isSafeInteger(response.id) || !['digest', 'text', 'parsed', 'error'].includes(response.type) ||
      response.type === 'error' && (typeof response.name !== 'string' || typeof response.message !== 'string')) {
      fail('Oops! Received an unexpected response from the background project worker.'); return;
    }
    const pending = current.pending.get(response.id);
    if (!pending) return;
    current.pending.delete(response.id); clearTimeout(pending.timer);
    if (response.type === 'error') pending.reject(Object.assign(new Error(response.message), { name: response.name }));
    else pending.resolve(response);
  });
  return (connection = current);
}
function send(current: Connection, request: Exclude<WireRequest, { type: 'release' }>): Promise<WireResponse> {
  if (current.failed) return Promise.reject(unavailable('Saving was interrupted—please try again.'));
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      current.fail('Oops! Packaging the project took too long and timed out.');
    }, 120_000);
    current.pending.set(request.id, { resolve, reject, timer });
    try { current.worker.postMessage(request); }
    catch (error) {
      current.pending.delete(request.id); clearTimeout(timer);
      reject(unavailable(error instanceof Error ? error.message : 'Oops! Couldn\'t prepare a snapshot of the project for saving.'));
    }
  });
}
export async function captureWire(value: unknown) {
  const current = getConnection(), id = ++serial;
  const response = await send(current, { type: 'capture', id, value });
  if (response.type !== 'digest' || !/^[0-9a-f]{64}$/.test(response.digest)) throw unavailable('Oops! Couldn\'t verify the project checksum while saving.');
  let released = false;
  const text = async (request: Exclude<WireRequest, { type: 'capture' | 'parse' | 'release' }>) => {
    const response = await send(current, request);
    if (response.type !== 'text' || typeof response.text !== 'string') throw unavailable('Oops! Couldn\'t package the project data for saving.');
    return response.text;
  };
  return {
    digest: response.digest,
    project: (body: Extract<WireRequest, {type:'project'}>['body']) => text({ type: 'project', id: ++serial, capture: id, body }),
    draft: (operationId: string) => text({ type: 'draft', id: ++serial, capture: id, operationId }),
    release() {
      if (released || current.failed) return;
      released = true;
      try { current.worker.postMessage({ type: 'release', capture: id } satisfies WireRequest); }
      catch { current.fail('Oops! The background project saver couldn\'t start.'); }
    },
  };
}
export async function parseWire<T>(text: string): Promise<T> {
  const response = await send(getConnection(), { type: 'parse', id: ++serial, text });
  if (response.type !== 'parsed') throw unavailable('Oops! Couldn\'t read the project data in the background worker.');
  return response.value as T;
}
