

import { useSyncExternalStore } from 'react';
import { confirmDialog } from './components/Ask';
import { discardPendingWrites } from './api/access';

let enabled = false, asked = false;
const listeners = new Set<() => void>();
function ask() {
  if (asked) return;
  asked = true;
  void fetch('/api/options').then((r) => r.ok ? r.json() as Promise<{ devReset?: boolean }> : null).then((body) => {
    if (body?.devReset !== true) return;
    enabled = true;
    listeners.forEach((l) => l());
  }).catch(() => { asked = false; });
}

export function useDevReset(): boolean {
  if (import.meta.env.MODE === 'demo') enabled = true;
  else ask();
  return useSyncExternalStore((l) => { listeners.add(l); return () => { listeners.delete(l); }; }, () => enabled);
}


export async function startOver(): Promise<void> {
  if (!await confirmDialog('Start over?', 'Everything is deleted.', ['Start over', 'Cancel'], true)) return;
  if (import.meta.env.MODE === 'demo') {
    discardPendingWrites();
    await (await import('./demo/transport')).resetDemo();
    location.replace(location.pathname);
    return;
  }

  discardPendingWrites();
  const r = await fetch('/api/dev/reset', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ operationId: crypto.randomUUID() }) });
  if (!r.ok) { discardPendingWrites(false); throw new Error(`${r.status} ${r.statusText}: /api/dev/reset`); }
  await Promise.allSettled([
    caches.keys().then((names) => Promise.all(names.map((n) => caches.delete(n)))),
    indexedDB.databases?.().then((dbs) => Promise.all(dbs.map((d) => d.name && new Promise((done) => { const q = indexedDB.deleteDatabase(d.name!); q.onsuccess = q.onerror = q.onblocked = () => done(null); })))),
    navigator.storage?.getDirectory?.().then(async (root) => { for await (const name of (root as unknown as { keys(): AsyncIterable<string> }).keys()) await root.removeEntry(name, { recursive: true }); }),
  ]);
  sessionStorage.clear();
  localStorage.clear();
  location.replace('/');
}
