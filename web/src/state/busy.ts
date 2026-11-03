import { setState } from './store';

export interface BusyToken { relabel(label: string): void; end(): void }
const live: { label: string }[] = [];
const project = () => setState({ loading: live.at(-1)?.label ?? null });

export function beginBusy(label: string, signal?: AbortSignal): BusyToken {
  const entry = { label };
  live.push(entry);
  project();
  const token: BusyToken = {
    relabel(next) { if (live.includes(entry) && entry.label !== next) { entry.label = next; project(); } },
    end() {
      const at = live.indexOf(entry);
      if (at < 0) return;
      live.splice(at, 1);
      signal?.removeEventListener('abort', token.end);
      project();
    },
  };
  if (signal?.aborted) token.end(); else signal?.addEventListener('abort', token.end, { once: true });
  return token;
}

export async function withBusy<T>(label: string, work: (token: BusyToken) => Promise<T>, signal?: AbortSignal): Promise<T> {
  const token = beginBusy(label, signal);
  try { return await work(token); } finally { token.end(); }
}

const startupEntry = { label: 'Connecting' };
live.push(startupEntry);
export const startupBusy: BusyToken = {
  relabel(next) { if (live.includes(startupEntry)) { startupEntry.label = next; project(); } },
  end() { const at = live.indexOf(startupEntry); if (at >= 0) { live.splice(at, 1); project(); } },
};

export function busySlot() {
  let token: BusyToken | null = null;
  return { begin(label: string) { token?.end(); return (token = beginBusy(label)); }, end() { token?.end(); token = null; } };
}
export const openingBusy = busySlot();
