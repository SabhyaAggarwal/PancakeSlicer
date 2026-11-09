let worker: Worker | null = null;
let id = 0;
const failures = new Set<(error: Error) => void>();

export function catalogWorker() {
  if (worker) return worker;
  const w = new Worker(new URL('./catalog.worker.ts', import.meta.url), { type: 'module' });
  const fail = (message: string) => {
    if (worker === w) worker = null;
    w.terminate();
    const error = new Error(message);
    for (const f of [...failures]) f(error);
  };
  w.addEventListener('error', (e) => { e.preventDefault(); fail(e.message || 'Oops! Profile catalog worker stopped'); });
  w.addEventListener('messageerror', () => fail('Catalog worker message unreadable'));
  return (worker = w);
}
export function workerId() { return ++id; }

export function onWorkerFailure(fail: (error: Error) => void): () => void {
  failures.add(fail);
  return () => void failures.delete(fail);
}
