let worker: Worker | null = null;
let id = 0;
export function catalogWorker() {
  return worker ??= new Worker(new URL('./catalog.worker.ts', import.meta.url), { type: 'module' });
}
export function workerId() { return ++id; }
