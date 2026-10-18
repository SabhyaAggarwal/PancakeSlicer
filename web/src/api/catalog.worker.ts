import { demoPresets, normalizeCatalog } from './catalog';
import type { PresetCatalog } from './types';

export interface CatalogIn {
  id: number;
  vendor: string;
  models: string[];
}

export type CatalogOut =
  | { id: number; type: 'catalog'; catalog: PresetCatalog; from: 'cache' | 'network' }
  | { id: number; type: 'done' }
  | { id: number; type: 'error'; message: string };

interface Entry {
  key: string;
  etag: string | null;
  catalog: PresetCatalog;
}

const VERSION = 1;

function db(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open('orca-catalog', VERSION);
    req.onupgradeneeded = () => {
      const d = req.result;
      if (d.objectStoreNames.contains('catalogs')) d.deleteObjectStore('catalogs');
      d.createObjectStore('catalogs', { keyPath: 'key' });
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function tx<T>(d: IDBDatabase, mode: IDBTransactionMode, op: (s: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    const req = op(d.transaction('catalogs', mode).objectStore('catalogs'));
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

const post = (m: CatalogOut) => (self as unknown as Worker).postMessage(m);

self.onmessage = async (e: MessageEvent<CatalogIn>) => {
  const { id, vendor, models } = e.data;
  try {
    const key = `${vendor}|${models.join(',')}`;
    const store = await db().catch(() => null);
    const cached = store ? await tx<Entry | undefined>(store, 'readonly', (s) => s.get(key)).catch(() => undefined) : undefined;
    if (cached) post({ id, type: 'catalog', catalog: cached.catalog, from: 'cache' });
    const r = await fetch(`/api/presets?vendor=${encodeURIComponent(vendor)}`, {
      cache: 'no-store',
      headers: cached?.etag ? { 'If-None-Match': cached.etag } : {},
    });
    if (r.status !== 304) {
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${(await r.text()).slice(0, 300)}`);
      const catalog = demoPresets(normalizeCatalog(await r.json(), vendor), models);
      if (!cached || JSON.stringify(cached.catalog) !== JSON.stringify(catalog)) post({ id, type: 'catalog', catalog, from: 'network' });
      if (store) await tx(store, 'readwrite', (s) => s.put({ key, etag: r.headers.get('ETag'), catalog } satisfies Entry)).catch(() => undefined);
    }
    post({ id, type: 'done' });
  } catch (err) {
    post({ id, type: 'error', message: err instanceof Error ? err.message : String(err) });
  }
};
