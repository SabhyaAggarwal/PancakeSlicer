import { compactCatalog, type CompactCatalog } from './compact-catalog';
import { normalizeCatalog } from './catalog';
import { loadRuntimeOptions, nativePresetCatalog, type RuntimeOptions } from '../option-inventory';
import optionsUrl from '../options.generated.json?url&no-inline';
import type { OptionDef, TabDesc } from '../options';

export type CatalogIn = { id: number; type: 'mock'; url: string } | { id: number; type: 'settings'; optionsUrl: string; tabUrl: string; mock?: boolean } | {
  type?: 'catalog';
  id: number;
  vendor: string;
  models: string[];
}

export type CatalogOut =
  | { id: number; type: 'catalog'; catalog: CompactCatalog; from: 'cache' | 'network' }
  | { id: number; type: 'settings'; options: Record<string, OptionDef>; tab: TabDesc }
  | { id: number; type: 'done' }
  | { id: number; type: 'error'; message: string };

interface Entry {
  key: string;
  etag: string | null;
  types: string;
  catalog: CompactCatalog;
}

const VERSION = 1;
const FORMAT = 6;

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
let mockSnapshot: Promise<CompactCatalog> | undefined;
async function asset(url: string) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${url}`);
  return r.json();
}

async function runtimeOptions(url: string, mock = false) {
  const key = `options|${FORMAT}|${url}|${mock}`;
  const store = await db().catch(() => null);
  try {
    const cached = store ? await tx<RuntimeOptions | undefined>(store, 'readonly', (s) => s.get(key)).catch(() => undefined) : undefined;
    const loaded = await loadRuntimeOptions(url, mock, cached);
    if (store && loaded !== cached)
      await tx(store, 'readwrite', (s) => s.put({ key, ...loaded })).catch(() => undefined);
    return loaded.options;
  } finally { store?.close(); }
}

self.onmessage = async (e: MessageEvent<CatalogIn>) => {
  const { id } = e.data;
  try {
    if (e.data.type === 'mock') {
      const url = e.data.url;
      mockSnapshot ??= (async () => {
        const response = await fetch(url);
        if (!response.ok) throw new Error('Mock presets failed to load');
        if (typeof DecompressionStream !== 'undefined' && response.body)
          return new Response(response.body.pipeThrough(new DecompressionStream('gzip'))).json();
        const { gunzipSync, strFromU8 } = await import('fflate');
        return JSON.parse(strFromU8(gunzipSync(new Uint8Array(await response.arrayBuffer()))));
      })().catch((error) => { mockSnapshot = undefined; throw error; });
      post({ id, type: 'catalog', catalog: await mockSnapshot, from: 'network' });
      post({ id, type: 'done' });
      return;
    }
    if (e.data.type === 'settings') {
      definitions ??= runtimeOptions(e.data.optionsUrl, e.data.mock).catch((error) => { definitions = null; throw error; });
      const [options, tab] = await Promise.all([definitions, asset(e.data.tabUrl)]);
      post({ id, type: 'settings', options: definitionsSent ? {} : options, tab });
      definitionsSent = true;
      return;
    }
    definitions ??= runtimeOptions(optionsUrl).catch((error) => { definitions = null; throw error; });
    const { vendor, models } = e.data;
    const key = `${FORMAT}|${vendor}|${models.join(',')}`;
    const store = await db().catch(() => null);
    const entry = store ? await tx<Entry | undefined>(store, 'readonly', (s) => s.get(key)).catch(() => undefined) : undefined;
    if (entry) post({ id, type: 'catalog', catalog: entry.catalog, from: 'cache' });
    const options = await definitions;
    const types = JSON.stringify([nativePresetCatalog(), Object.entries(options).map(([key, def]) => [key, def.type])]);
    const cached = entry?.types === types ? entry : undefined;
    const r = await fetch(`/api/presets?vendor=${encodeURIComponent(vendor)}`, {
      cache: 'no-store',
      headers: cached?.etag ? { 'If-None-Match': cached.etag } : {},
    });
    if (r.status !== 304) {
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${(await r.text()).slice(0, 300)}`);
      const catalog = compactCatalog(normalizeCatalog(await r.json(), vendor, nativePresetCatalog(), models));
      if (!cached || JSON.stringify(cached.catalog) !== JSON.stringify(catalog)) post({ id, type: 'catalog', catalog, from: 'network' });
      if (store) await tx(store, 'readwrite', (s) => s.put({ key, etag: r.headers.get('ETag'), types, catalog } satisfies Entry)).catch(() => undefined);
    }
    post({ id, type: 'done' });
  } catch (err) {
    post({ id, type: 'error', message: err instanceof Error ? err.message : String(err) });
  }
};
