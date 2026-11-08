import { responseError } from '../api/access';

export interface CatalogModel { id: string; name: string; cover?: string; nozzles: string[]; machines: Record<string, string> }
export interface CatalogVendor { id: string; name: string; models: CatalogModel[] }
export interface CatalogFamily { id: string; name: string; brand?: string; material?: string; colour?: string; models: string[]; variants: Record<string, string>; fallback?: string }
export type CatalogRead = { vendors: CatalogVendor[] } | { families: CatalogFamily[] };

export const PRINTER_CATALOG = '/api/printer-catalog';
export const filamentCatalogPath = (vendor: string) => `/api/filament-catalog?vendor=${encodeURIComponent(vendor)}`;

const reads = new Map<string, Promise<unknown>>(), bodies = new Map<string, unknown>();
export function readCatalog<T>(path: string): Promise<T> {
  let r = reads.get(path);
  if (!r) reads.set(path, (r = fetch(path).then(async (res) => { if (!res.ok) throw await responseError(res); return res.json(); })
    .then((body) => { bodies.set(path, body); return body; }, (e) => { reads.delete(path); throw e; })));
  return r as Promise<T>;
}
export const loadedCatalog = <T,>(path: string) => bodies.get(path) as T | undefined;

export const printerCatalog = () => readCatalog<{ vendors: CatalogVendor[] }>(PRINTER_CATALOG).then((b) => b.vendors);
export const filamentCatalog = (vendor: string) => readCatalog<{ families: CatalogFamily[] }>(filamentCatalogPath(vendor)).then((b) => b.families);

export const FIRST_SCREEN = 24;
const covers = new Map<string, HTMLImageElement>();
export function preloadCovers(vendors: CatalogVendor[], ids: Iterable<string>) {
  for (const id of new Set(ids)) preloadImages((vendors.find((v) => v.id === id)?.models.slice(0, FIRST_SCREEN) ?? []).flatMap((m) => m.cover ? [m.cover] : []));
}
export function preloadImages(urls: Iterable<string>) {
  for (const url of urls) {
    if (covers.has(url)) continue;
    const img = new Image();
    img.fetchPriority = 'low';
    img.src = url;
    covers.set(url, img);
    img.decode().catch(() => covers.delete(url));
  }
}
