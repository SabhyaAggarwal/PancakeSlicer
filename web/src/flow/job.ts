import type { Api } from '../api/client';
import type { ProgressEvent } from '../api/types';
export function waitForJob(api: Api, id: string, signal?: AbortSignal, progress?: (p: ProgressEvent) => void): Promise<void> {
  return new Promise((resolve, reject) => {
    let settled = false, unsubscribe = () => {}, timer: ReturnType<typeof setTimeout> | undefined;
    const finish = (error?: unknown) => {
      if (settled) return;
      settled = true; unsubscribe(); clearTimeout(timer); clearTimeout(deadline);
      signal?.removeEventListener('abort', abort);
      if (error) reject(error); else resolve();
    };
    const abort = () => {
      void api.cancel(id).catch(() => {});
      finish(signal?.reason ?? new DOMException('Canceled', 'AbortError'));
    };
    const deadline = setTimeout(() => {
      void api.cancel(id).catch(() => {});
      finish(new Error('Project export timed out. Retry when the slicing service is available.'));
    }, 5 * 60_000);
    const poll = async () => {
      let timeout: ReturnType<typeof setTimeout> | undefined;
      try {
        const status = await Promise.race([api.status(id), new Promise<never>((_, fail) => {
          timeout = setTimeout(() => fail(new Error('The slicing service stopped responding. Retry project export.')), 10_000);
        })]);
        if (settled) return;
        if (status.state === 'done') finish();
        else if (status.state !== 'running' && status.state !== 'queued') finish(new Error(status.error ?? `Project export ${status.state}`));
        else timer = setTimeout(() => void poll(), 5000);
      } catch (e) { finish(e); }
      finally { clearTimeout(timeout); }
    };
    signal?.addEventListener('abort', abort, { once: true });
    if (signal?.aborted) { abort(); return; }
    try {
      unsubscribe = api.events(id, { progress: (p) => { if (!settled) progress?.(p); }, done: () => finish(), error: (message) => finish(new Error(message)) });
      if (settled) unsubscribe();
      else timer = setTimeout(() => void poll(), 5000);
    } catch (e) { finish(e); }
  });
}
