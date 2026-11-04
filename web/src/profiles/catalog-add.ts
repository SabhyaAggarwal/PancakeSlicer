import { useEffect, useState } from 'react';
import type { Config, UserPreset } from '../api/types';
import { responseError } from '../api/access';
import { getState } from '../state/store';
import { findCatalogBase } from './model';
import { uniqueName } from './catalog';
import { profileCreation } from './transactions';

export interface CatalogModel { id: string; name: string; cover?: string; nozzles: string[]; machines: Record<string, string> }
export interface CatalogVendor { id: string; name: string; models: CatalogModel[] }
export interface CatalogFamily { id: string; name: string; brand?: string; material?: string; colour?: string; models: string[]; variants: Record<string, string>; fallback?: string }

const reads = new Map<string, Promise<unknown>>();
function read<T>(path: string): Promise<T> {
  let r = reads.get(path);
  if (!r) reads.set(path, (r = fetch(path).then(async (res) => { if (!res.ok) throw await responseError(res); return res.json(); })
    .catch((e) => { reads.delete(path); throw e; })));
  return r as Promise<T>;
}
const printerCatalog = () => read<{ vendors: CatalogVendor[] }>('/api/printer-catalog').then((b) => b.vendors);
const filamentCatalog = (vendor: string) => read<{ families: CatalogFamily[] }>(`/api/filament-catalog?vendor=${encodeURIComponent(vendor)}`).then((b) => b.families);

export type CatalogRead = { vendors: CatalogVendor[] } | { families: CatalogFamily[] };
export function useCatalog(type: 'machine' | 'filament' | 'process', vendor: string, adding: boolean): { data?: CatalogRead; error?: string } {
  const [state, setState] = useState<{ key: string; data?: CatalogRead; error?: string }>({ key: '' });
  const key = adding && type !== 'process' ? `${type}:${type === 'filament' ? vendor : ''}` : '';
  useEffect(() => {
    if (!key) return;
    let live = true;
    (type === 'machine' ? printerCatalog().then((vendors) => ({ vendors })) : filamentCatalog(vendor).then((families) => ({ families })))
      .then((data) => live && setState({ key, data }), (e) => live && setState({ key, error: e instanceof Error ? e.message : String(e) }));
    return () => { live = false; };
  }, [key]);
  return state.key === key ? state : {};
}

const plain = (name: string) => name.replace(/\s+@.*$/, '').trim();
const anchor = (name: string, vendor: string) => ({ name, engineName: name, version: '', vendor });

export async function addCatalogPrinter(vendor: string, model: CatalogModel, nozzle: string): Promise<string> {
  const machine = model.machines[nozzle], api = getState().api;
  if (!machine) throw new Error(`${model.name} has no ${nozzle} mm nozzle.`);
  const preset = findCatalogBase(getState(), 'machine', machine).base ?? (api && (await api.presets(vendor, [], undefined, [machine])).machines.find((p) => p.name === machine));
  const defaults = (preset as { print_defaults?: Config } | undefined)?.print_defaults;
  return profileCreation((s) => {
    const name = uniqueName(s, 'machine', plain(nozzle === '0.4' || model.nozzles.length === 1 ? model.name : `${model.name} ${nozzle} nozzle`));
    return { id: `profile:${crypto.randomUUID()}`, type: 'machine', kind: 'profile', name, revision: 0, inherits: machine,
      base: anchor(machine, vendor), overlay: {}, overrides: {},
      ...(defaults && Object.keys(defaults).length ? { printOverrides: { ...defaults } } : {}) } as UserPreset;
  }).commit();
}

export function addCatalogFamily(family: CatalogFamily, colour?: string, label?: string): Promise<string> {
  return profileCreation((s) => {
    const machine = s.userPresets.find((p) => p.type === 'machine' && !p.archived)?.base?.name ?? '';
    const variant = family.variants[machine] ?? family.fallback ?? Object.values(family.variants)[0] ?? family.name;
    const overlay: Config = colour ? { filament_colour: [colour] } : {};
    return { id: `profile:${crypto.randomUUID()}`, type: 'filament', kind: 'profile', name: uniqueName(s, 'filament', plain(label ?? family.name)), revision: 0,
      inherits: variant, base: anchor(variant, family.id.split('/')[0]),
      overlay, overrides: overlay, metadata: { family: family.id } } as UserPreset;
  }).commit();
}
