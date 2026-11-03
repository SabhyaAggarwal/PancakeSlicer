import type { Config, UserPreset } from '../api/types';
import { findProfile, profileKey, withProfileIdentity, type ProfileKey } from './identity';
import { catalogAnchor, changedKeys, normalizeUserPreset, resolveProfile, type ProfileCatalog } from './model';
import { emptyPrinterLibrary, emptyPrinterSetup, type PrinterProfileLibrary, type PrinterMaterialSetup, type ProfileRecovery } from './ownership';

export interface MaterialLibraryState extends ProfileCatalog {
  printer: string;
  filamentSlots: string[];
  slotColours: (string | undefined)[];
  filamentEdits: Record<string, Config>;
  mixes: PrinterMaterialSetup['mixes'];
  printerLibrary?: PrinterProfileLibrary;
}
export interface MaterialLibraryPlan {
  userPresets: UserPreset[];
  printerLibrary: PrinterProfileLibrary;
  recoveries: ProfileRecovery[];
  materialId?: ProfileKey;
}
export interface PrinterMaterialsPlan extends MaterialLibraryPlan {
  printer: ProfileKey;
  filamentSlots: string[];
  slotColours: (string | undefined)[];
  filamentEdits: Record<ProfileKey, Config>;
  mixes: PrinterMaterialSetup['mixes'];
  referenceMap: Record<string, ProfileKey>;
}
const libraryOf = (s: MaterialLibraryState) => s.printerLibrary ?? emptyPrinterLibrary();
const clone = <T>(value: T): T => structuredClone(value);
const putSetup = (library: PrinterProfileLibrary, setup: PrinterMaterialSetup): PrinterProfileLibrary => ({
  ...library, revision: library.revision + 1,
  setups: { ...library.setups, [setup.printerId]: { ...setup, revision: setup.revision + 1 } },
});
export function planAddMaterial(s: MaterialLibraryState, sourceKey: string, options: {
  printerId: ProfileKey; operationId: string; duplicate?: boolean; allowCrossPrinter?: boolean;
}): MaterialLibraryPlan {
  const library = libraryOf(s), owner = findProfile(s, 'machine', options.printerId);
  const unchanged = (recovery: ProfileRecovery): MaterialLibraryPlan => ({ userPresets: s.userPresets, printerLibrary: library, recoveries: [recovery] });
  if (!owner || profileKey(owner) !== options.printerId)
    return unchanged({ type: 'machine', key: options.printerId, reason: 'missing-owner' });
  const setup = library.setups[options.printerId] ?? emptyPrinterSetup(options.printerId);
  const retry = s.userPresets.find(p => p.type === 'filament' && p.printerId === options.printerId && p.provenance?.operationId === options.operationId);
  const source = findProfile(s, 'filament', sourceKey, options.printerId) ?? findProfile(s, 'filament', sourceKey);
  const resolved = resolveProfile(s, 'filament', source ? profileKey(source) : sourceKey);
  if (!retry && resolved.issue && !resolved.source?.snapshot)
    return unchanged({ type: 'filament', key: sourceKey, reason: resolved.issue.code });
  const reusable = retry ?? (!options.duplicate && source ? source : undefined);
  let material = reusable;
  if (!material) {
    const anchor = resolved.base ? catalogAnchor(resolved.base) : source?.snapshot?.base;
    if (!anchor) return unchanged({ type: 'filament', key: sourceKey, reason: 'missing-base', snapshot: source?.snapshot });
    const config = clone(resolved.config);
    const overlay = resolved.base ? changedKeys(config, resolved.base.config) : config;
    material = {
      id: `profile:${crypto.randomUUID()}`, type: 'filament', kind: 'profile',
      name: source?.name ?? resolved.name ?? sourceKey, revision: 0, base: clone(anchor), inherits: anchor.name,
      overlay, overrides: overlay, baseline: Object.fromEntries(Object.keys(overlay).map(key => [key, resolved.base?.config[key] ?? null])),
      metadata: source?.metadata && clone(source.metadata), snapshot: { config, base: clone(anchor) },
      provenance: { source: source ? 'profile' : 'catalog', sourceId: source && profileKey(source), sourceName: resolved.name ?? sourceKey,
        sourceRevision: source?.revision, operationId: options.operationId },
    };
  }
  const id = profileKey(material), included = setup.materialIds.includes(id);
  return { userPresets: reusable ? s.userPresets : [...s.userPresets, material], materialId: id,
    printerLibrary: included ? library : putSetup(library, { ...setup, materialIds: [...setup.materialIds, id] }),
    recoveries: resolved.issue ? [{ type: 'filament', key: id, reason: resolved.issue.code, snapshot: material.snapshot }] : [],
  };
}

export function capturePrinterMaterials(s: MaterialLibraryState): PrinterProfileLibrary {
  const library = libraryOf(s), printer = findProfile(s, 'machine', s.printer);
  if (!printer) return library;
  const id = profileKey(printer), setup = library.setups[id] ?? emptyPrinterSetup(id);
  if (s.filamentSlots.some(key => !findProfile(s, 'filament', key))) return library;
  const slots = s.filamentSlots.map((key, index) => ({ id: setup.slots[index]?.id ?? `slot:${crypto.randomUUID()}`,
    materialId: profileKey(findProfile(s, 'filament', key)!), colour: s.slotColours[index] }));
  const tuning = { ...setup.tuning, ...Object.fromEntries(Object.entries(s.filamentEdits).flatMap(([key, config]) => {
    const material = findProfile(s, 'filament', key); return material ? [[profileKey(material), clone(config)]] : [];
  })) };
  for (const slot of slots) if (!Object.keys(s.filamentEdits[slot.materialId] ?? {}).length) delete tuning[slot.materialId];
  const next = { ...setup, slots, materialIds: [...new Set([...setup.materialIds, ...slots.map(slot => slot.materialId)])], tuning, mixes: clone(s.mixes) };
  if (JSON.stringify({ ...next, revision: 0 }) === JSON.stringify({ ...setup, revision: 0 })) return library;
  return putSetup(library, next);
}

export function planMaterialMigration(input: MaterialLibraryState): PrinterMaterialsPlan {
  let s = { ...input, userPresets: input.userPresets.map(p => p.id && p.kind && p.revision !== undefined ? p : normalizeUserPreset(p)) };
  if (s.userPresets.every((p, i) => p === input.userPresets[i])) s.userPresets = input.userPresets;
  const printer = findProfile(s, 'machine', s.printer), id = printer && profileKey(printer);
  const result: PrinterMaterialsPlan = { userPresets: s.userPresets, printerLibrary: libraryOf(s), printer: id ?? s.printer,
    filamentSlots: [...s.filamentSlots], slotColours: [...s.slotColours], filamentEdits: { ...s.filamentEdits }, mixes: clone(s.mixes), referenceMap: {}, recoveries: [] };
  if (!id) { result.recoveries.push({ type: 'machine', key: s.printer, reason: 'missing-owner' }); return result; }
  for (const [index, key] of s.filamentSlots.entries()) {
    const source = findProfile(s, 'filament', key, id) ?? findProfile(s, 'filament', key);
    const sourceId = source ? profileKey(source) : key;
    const added = planAddMaterial({ ...s, userPresets: result.userPresets, printerLibrary: result.printerLibrary }, sourceId,
      { printerId: id, operationId: `migration:1:${id}:${sourceId}` });
    result.userPresets = added.userPresets; result.printerLibrary = added.printerLibrary; result.recoveries.push(...added.recoveries);
    if (!added.materialId) continue;
    result.filamentSlots[index] = added.materialId; result.referenceMap[key] = added.materialId;
    if (s.filamentEdits[key]) { result.filamentEdits[added.materialId] = clone(s.filamentEdits[key]); if (key !== added.materialId) delete result.filamentEdits[key]; }
  }
  result.printerLibrary = capturePrinterMaterials({ ...s, ...result });
  return result;
}

export function planPrinterSwitch(s: MaterialLibraryState, printerKey: string): PrinterMaterialsPlan {
  const library = capturePrinterMaterials(s), printer = findProfile(s, 'machine', printerKey), id = printer && profileKey(printer);
  const setup = id && library.setups[id];
  const result: PrinterMaterialsPlan = { userPresets: s.userPresets, printerLibrary: library, printer: id ?? printerKey,
    filamentSlots: setup ? setup.slots.map(slot => slot.materialId) : [...s.filamentSlots], slotColours: setup ? setup.slots.map(slot => slot.colour) : [...s.slotColours],
    filamentEdits: setup ? clone(setup.tuning) : {}, mixes: setup ? clone(setup.mixes) : clone(s.mixes), referenceMap: {}, recoveries: [] };
  if (!id) result.recoveries.push({ type: 'machine', key: printerKey, reason: 'missing-owner' });
  if (setup) for (const key of setup.materialIds) if (!findProfile(s, 'filament', key))
    result.recoveries.push({ type: 'filament', key, reason: 'missing-profile' });
  return result;
}
export function planRemoveMaterialSlot(library: PrinterProfileLibrary, printerId: string, slotId: string): PrinterProfileLibrary {
  const setup = library.setups[printerId];
  if (!setup?.slots.some(slot => slot.id === slotId)) return library;
  return putSetup(library, { ...setup, slots: setup.slots.filter(slot => slot.id !== slotId),
    assignments: Object.fromEntries(Object.entries(setup.assignments).filter(([, slot]) => slot !== slotId)) });
}
export function renameOwnedProfile(profile: UserPreset, name: string): UserPreset {
  return { ...withProfileIdentity(profile), name, aliases: [...new Set([...(profile.aliases ?? []), profile.name])], revision: (profile.revision ?? 0) + 1 };
}
