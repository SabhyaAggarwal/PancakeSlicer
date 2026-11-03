import type { CompactCatalog } from './compact-catalog';
import { normalizeCatalog } from './catalog';
import { loadRuntimeOptions, nativePresetCatalog, type RuntimeOptions } from '../option-inventory';
import { DEMO_MODELS, VENDORS, presetsUrl } from './catalog-scope';
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
  | { id: number; type: 'settings'; options: Record<string, OptionDef>; tooltips?: string | null; tab: TabDesc }
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

let definitions: Promise<RuntimeOptions> | null = null;
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
  return loaded;
}

const catalogKey = (vendor: string, models: string[]) => `${FORMAT}|${vendor}|${models.join(',')}`;
const typesOf = (options: Record<string, OptionDef>) => JSON.stringify([nativePresetCatalog(), Object.entries(options).map(([key, def]) => [key, def.type])]);
const warm = (async () => {
  definitions ??= runtimeOptions(optionsUrl).catch((error) => { definitions = null; throw error; });
  const types = typesOf((await definitions).options);
  await Promise.all(VENDORS.map(async (vendor) => {
    const r = await fetch(presetsUrl(vendor, DEMO_MODELS), { cache: 'no-cache' });
    if (r.ok) remember({ key: catalogKey(vendor, DEMO_MODELS), etag: r.headers.get('ETag'), types, catalog: normalizeCatalog(await r.json(), vendor, nativePresetCatalog()) });
  }));
})().catch(() => {});

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
      const [loaded, tab] = await Promise.all([definitions, asset(e.data.tabUrl)]);
      post({ id, type: 'settings', ...(definitionsSent ? { options: {} } : { options: loaded.options, tooltips: loaded.tooltips }), tab });
      definitionsSent = true;
      return;
    }
    definitions ??= runtimeOptions(optionsUrl).catch((error) => { definitions = null; throw error; });
    const { vendor, models } = e.data;
    const key = catalogKey(vendor, models);
    await warm;
    const entry = catalogs.get(key)?.entry;
    const read = (etag?: string | null) => fetch(presetsUrl(vendor, models), {
      cache: 'no-cache',
      headers: etag ? { 'If-None-Match': etag } : {},
    });
    const early = entry ? undefined : read();
    const { options } = await definitions;
    const types = typesOf(options);
    const cached = entry?.types === types ? entry : undefined;
    if(cached)post({id,type:'catalog',catalog:cached.catalog,from:'cache'});
    const r = await (early ?? read(cached?.etag));
    if (r.status !== 304) {
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${(await r.text()).slice(0, 300)}`);
      const catalog = normalizeCatalog(await r.json(), vendor, nativePresetCatalog());
      if (!cached || JSON.stringify(cached.catalog) !== JSON.stringify(catalog)) post({ id, type: 'catalog', catalog, from: 'network' });
      remember({key,etag:r.headers.get('ETag'),types,catalog});
    } else if(!cached)throw new Error('Catalog validation returned 304 without a matching memory entry.');
    post({ id, type: 'done' });
  } catch (err) {
    post({ id, type: 'error', message: err instanceof Error ? err.message : String(err) });
  }
};
