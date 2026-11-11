import { SPEED_KEYS } from './speed-rules';
import { useEffect, useState } from 'react';
import type { Config, UserPreset } from '../api/types';
import { getState, type State } from '../state/store';
import { findCatalogBase, resolveProfile } from './model';
import { uniqueName } from './catalog';
import { profileKey } from './identity';
import { profileBatchCreation } from './transactions';
import { filamentCatalogPath, loadedCatalog, PRINTER_CATALOG, readCatalog, type CatalogFamily, type CatalogModel, type CatalogRead } from './catalog-reads';

export function useCatalog(type: 'machine' | 'filament' | 'process', vendor: string, adding: boolean): { data?: CatalogRead; error?: string } {
  const [state, setState] = useState<{ path: string; data?: CatalogRead; error?: string }>({ path: '' });
  const path = adding && type !== 'process' ? type === 'machine' ? PRINTER_CATALOG : filamentCatalogPath(vendor) : '';
  const loaded = path ? loadedCatalog<CatalogRead>(path) : undefined;
  useEffect(() => {
    if (!path || loadedCatalog(path)) return;
    let live = true;
    readCatalog<CatalogRead>(path).then((data) => live && setState({ path, data }), (e) => live && setState({ path, error: e instanceof Error ? e.message : String(e) }));
    return () => { live = false; };
  }, [path]);
  return loaded ? { data: loaded } : state.path === path ? state : {};
}

const plain = (name: string) => name.replace(/\s+@.*$/, '').trim();
const anchor = (name: string, vendor: string) => ({ name, engineName: name, version: '', vendor });

export interface PrinterChoice { vendor: string; model: CatalogModel; nozzle: string }
export interface FamilyChoice { family: CatalogFamily; colour?: string; label?: string }
export interface AddResult { keys: string[]; existing: string[]; skipped: string[]; chosen: string[] }

export async function printDefaults({ vendor, model, nozzle }: PrinterChoice): Promise<Config | undefined> {
  const machine = model.machines[nozzle], api = getState().api;
  if (!machine) throw new Error(`${model.name} doesn't have a ${nozzle} mm nozzle preset.`);
  const preset = findCatalogBase(getState(), 'machine', machine).base ?? (api && (await api.presets(vendor, [], undefined, [machine])).machines.find((p) => p.name === machine));
  const supplied = (preset as { print_defaults?: Config } | undefined)?.print_defaults;
  return supplied && Object.fromEntries(Object.entries(supplied).filter(([key]) => !SPEED_KEYS.has(key)));
}
const owned = (s: State) => s.userPresets.filter((p) => !p.archived);
const namer = (s: State) => {
  const planned: UserPreset[] = [];
  return { name: (type: 'machine' | 'filament', wanted: string) => uniqueName({ ...s, userPresets: [...s.userPresets, ...planned] }, type, wanted), keep: (p: UserPreset) => { planned.push(p); return p; } };
};

export async function addCatalogPrinters(choices: PrinterChoice[]): Promise<AddResult> {
  const defaults = await Promise.all(choices.map(printDefaults));
  let existing: string[] = [], chosen: string[] = [];
  const result = await profileBatchCreation((s) => {
    const names = namer(s), taken = new Map(owned(s).filter((p) => p.type === 'machine').map((p) => [p.base?.name ?? p.inherits, profileKey(p)]));
    const skipped: string[] = [], records: UserPreset[] = [];
    existing = []; chosen = [];
    choices.forEach((choice, i) => {
      const { vendor, model, nozzle } = choice, machine = model.machines[nozzle], label = plain(nozzle === '0.4' || model.nozzles.length === 1 ? model.name : `${model.name} ${nozzle} nozzle`);
      if (taken.has(machine)) { skipped.push(label); existing.push(taken.get(machine)!); chosen.push(taken.get(machine)!); return; }
      taken.set(machine, '');
      const id = `profile:${crypto.randomUUID()}`; chosen.push(id);
      records.push(names.keep({ id, type: 'machine', kind: 'profile', name: names.name('machine', label), revision: 0, inherits: machine,
        base: anchor(machine, vendor), overlay: {}, overrides: {}, ...(defaults[i] && Object.keys(defaults[i]!).length ? { printOverrides: { ...defaults[i] } } : {}) } as UserPreset));
    });
    return { records, skipped };
  }).commit();
  return { keys: result.keys, existing, skipped: result.skipped, chosen };
}
export async function addCatalogPrinter(vendor: string, model: CatalogModel, nozzle: string): Promise<string> {
  const { keys, skipped } = await addCatalogPrinters([{ vendor, model, nozzle }]);
  if (!keys.length) throw new Error(`${skipped[0]} is already in your printers list.`);
  return keys[0];
}

export async function addCatalogFamilies(choices: FamilyChoice[]): Promise<AddResult> {
  let existing: string[] = [], chosen: string[] = [];
  const result = await profileBatchCreation((s) => {
    const names = namer(s), machine = resolveProfile(s, 'machine', s.printer).base?.name ?? '';
    const colourOf = (c?: unknown) => String([c ?? ''].flat()[0] ?? '').toUpperCase();
    const has = new Map(owned(s).filter((p) => p.type === 'filament').map((p) => [`${p.base?.name ?? p.inherits}\0${colourOf(p.overlay?.filament_colour)}`, profileKey(p)]));
    const skipped: string[] = [], records: UserPreset[] = [];
    existing = []; chosen = [];
    for (const { family, colour, label } of choices) {
      const variant = family.variants[machine] ?? family.fallback ?? Object.values(family.variants)[0] ?? family.name;
      const key = `${variant}\0${colourOf(colour)}`, wanted = plain(label ?? family.name);
      if (has.has(key)) { skipped.push(wanted); existing.push(has.get(key)!); chosen.push(has.get(key)!); continue; }
      has.set(key, '');
      const overlay: Config = colour ? { filament_colour: [colour] } : {};
      const id = `profile:${crypto.randomUUID()}`; chosen.push(id);
      records.push(names.keep({ id, type: 'filament', kind: 'profile', name: names.name('filament', wanted), revision: 0,
        inherits: variant, base: anchor(variant, family.id.split('/')[0]), overlay, overrides: overlay, metadata: { family: family.id } } as UserPreset));
    }
    return { records, skipped };
  }).commit();
  return { keys: result.keys, existing, skipped: result.skipped, chosen };
}
export async function addCatalogFamily(family: CatalogFamily, colour?: string, label?: string): Promise<string> {
  const { keys, skipped } = await addCatalogFamilies([{ family, colour, label }]);
  if (!keys.length) throw new Error(`${skipped[0]} is already in your filaments list.`);
  return keys[0];
}
