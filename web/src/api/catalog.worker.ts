import { demoPresets, normalizeCatalog } from './catalog';
import { loadRuntimeOptions, nativePresetCatalog } from '../option-inventory';
import optionsUrl from '../options.generated.json?url&no-inline';
import type { OptionDef, TabDesc } from '../options';
import type { PresetCatalog } from './types';

export type CatalogIn = { id: number; type: 'settings'; optionsUrl: string; tabUrl: string; mock?: boolean } | {
  type?: 'catalog';
  id: number;
  vendor: string;
  models: string[];
}

export type CatalogOut =
  | { id: number; type: 'catalog'; catalog: PresetCatalog; from: 'cache' | 'network' }
  | { id: number; type: 'settings'; options: Record<string, OptionDef>; tab: TabDesc }
  | { id: number; type: 'done' }
  | { id: number; type: 'error'; message: string };

interface Entry {
  key: string;
  etag: string | null;
  types: string;
  catalog: PresetCatalog;
}

const VERSION = 1;
const FORMAT = 4;

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

let definitions: Promise<Record<string, OptionDef>> | null = null;
let definitionsSent = false;
async function asset(url: string) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${url}`);
  return r.json();
}

self.onmessage = async (e: MessageEvent<CatalogIn>) => {
  const { id } = e.data;
  try {
    if (e.data.type === 'settings') {
      definitions ??= loadRuntimeOptions(e.data.optionsUrl, e.data.mock).catch((error) => { definitions = null; throw error; });
      const [options, tab] = await Promise.all([definitions, asset(e.data.tabUrl)]);
      post({ id, type: 'settings', options: definitionsSent ? {} : options, tab });
      definitionsSent = true;
      return;
    }
    definitions ??= loadRuntimeOptions(optionsUrl).catch((error) => { definitions = null; throw error; });
    const options = await definitions;
    const types = JSON.stringify([nativePresetCatalog(), Object.entries(options).map(([key, def]) => [key, def.type])]);
    const { vendor, models } = e.data;
    const key = `${FORMAT}|${vendor}|${models.join(',')}`;
    const store = await db().catch(() => null);
    const entry = store ? await tx<Entry | undefined>(store, 'readonly', (s) => s.get(key)).catch(() => undefined) : undefined;
    const cached = entry?.types === types ? entry : undefined;
    if (cached) post({ id, type: 'catalog', catalog: cached.catalog, from: 'cache' });
    const r = await fetch(`/api/presets?vendor=${encodeURIComponent(vendor)}`, {
      cache: 'no-store',
      headers: cached?.etag ? { 'If-None-Match': cached.etag } : {},
    });
    if (r.status !== 304) {
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${(await r.text()).slice(0, 300)}`);
      const catalog = demoPresets(normalizeCatalog(await r.json(), vendor, nativePresetCatalog()), models);
      if (!cached || JSON.stringify(cached.catalog) !== JSON.stringify(catalog)) post({ id, type: 'catalog', catalog, from: 'network' });
      if (store) await tx(store, 'readwrite', (s) => s.put({ key, etag: r.headers.get('ETag'), types, catalog } satisfies Entry)).catch(() => undefined);
    }
    post({ id, type: 'done' });
  } catch (err) {
    post({ id, type: 'error', message: err instanceof Error ? err.message : String(err) });
  }
};
