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

const FORMAT = 7;
const catalogs = new Map<string, {entry:Entry; bytes:number}>();
const runtimeCache = new Map<string, RuntimeOptions>();
let catalogBytes = 0;
function remember(entry:Entry) {
  const prior=catalogs.get(entry.key); if(prior)catalogBytes-=prior.bytes;
  catalogs.delete(entry.key);
  const bytes=JSON.stringify(entry.catalog).length*2;
  if(bytes>24*1024*1024)return;
  catalogs.set(entry.key,{entry,bytes});catalogBytes+=bytes;
  while(catalogs.size>6 || catalogBytes>24*1024*1024){
    const first=catalogs.keys().next().value!;catalogBytes-=catalogs.get(first)!.bytes;catalogs.delete(first);
  }
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

async function runtimeOptions(url:string,mock=false) {
  const key=`options|${FORMAT}|${url}|${mock}`;
  const loaded=await loadRuntimeOptions(url,mock,runtimeCache.get(key));
  runtimeCache.delete(key);runtimeCache.set(key,loaded);
  while(runtimeCache.size>2)runtimeCache.delete(runtimeCache.keys().next().value!);
  return loaded.options;
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
    const entry = catalogs.get(key)?.entry;
    const options = await definitions;
    const types = JSON.stringify([nativePresetCatalog(), Object.entries(options).map(([key, def]) => [key, def.type])]);
    const cached = entry?.types === types ? entry : undefined;
    if(cached)post({id,type:'catalog',catalog:cached.catalog,from:'cache'});
    const r = await fetch(`/api/presets?${new URLSearchParams([['vendor', vendor], ...models.map((m) => ['model', m])])}`, {
      cache: 'no-cache',
      headers: cached?.etag ? { 'If-None-Match': cached.etag } : {},
    });
    if (r.status !== 304) {
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${(await r.text()).slice(0, 300)}`);
      const catalog = compactCatalog(normalizeCatalog(await r.json(), vendor, nativePresetCatalog()));
      if (!cached || JSON.stringify(cached.catalog) !== JSON.stringify(catalog)) post({ id, type: 'catalog', catalog, from: 'network' });
      remember({key,etag:r.headers.get('ETag'),types,catalog});
    } else if(!cached)throw new Error('Catalog validation returned 304 without a matching memory entry.');
    post({ id, type: 'done' });
  } catch (err) {
    post({ id, type: 'error', message: err instanceof Error ? err.message : String(err) });
  }
};
