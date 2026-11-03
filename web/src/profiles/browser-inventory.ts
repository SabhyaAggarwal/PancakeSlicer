import type { ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import type { State } from '../state/store';
import { persistProfileLibrary } from '../persist/profile-library';
import { normalizeUserPreset, storedProfile } from './model';
import { capturePrinterMaterials } from './material-library';
import type { PrinterProfileLibrary, ProfileLibraryDocument } from './ownership';

const legacyKeys = ['orca.userPresets', 'orca.profileOverlays.v1', 'orca.profileBases.v1', 'orca.profileDefaults.v1', 'orca.printer', 'orca.vendor'] as const;
export interface BrowserProfileInventory {
  schema: 1;
  scope: 'profile-library-and-working-setup';
  complete: boolean;
  storage: Record<string, string | null>;
  legacyDocument: ProfileLibraryDocument | null;
  profiles: ProfileOverlay[];
  catalog: SystemPreset[];
  printers: PrinterProfileLibrary;
  working: Pick<State, 'printer' | 'process' | 'filamentSlots' | 'slotColours' | 'filamentEdits' | 'mixes' | 'dirty' | 'projectPresets' | 'profileSnapshots' | 'projectProfileContexts'>;
  issues: string[];
}
const equal = (a: unknown, b: unknown): boolean => {
  if (a === b) return true;
  if (!a || !b || typeof a !== 'object' || typeof b !== 'object') return false;
  if (Array.isArray(a) || Array.isArray(b)) return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => equal(v, b[i]));
  const left = a as Record<string, unknown>, right = b as Record<string, unknown>;
  const keys=Object.keys(left).filter(k=>left[k]!==undefined),other=Object.keys(right).filter(k=>right[k]!==undefined);
  return keys.length===other.length && keys.every(k=>Object.hasOwn(right,k) && equal(left[k],right[k]));
};
async function readLegacyDocument(): Promise<ProfileLibraryDocument | null> {
  if (typeof indexedDB === 'undefined') throw new Error('Browser profile storage is unavailable; its absence has not been established.');
  if (!indexedDB.databases) throw new Error('This browser cannot enumerate legacy profile storage. Export it from the previous app before adoption.');
  if (!(await indexedDB.databases()).some(db => db.name === 'orca-profile-library')) return null;
  return new Promise((resolve, reject) => {
    const open = indexedDB.open('orca-profile-library');
    open.onupgradeneeded = () => { open.transaction?.abort(); };
    open.onerror = () => reject(open.error);
    open.onblocked = () => reject(new Error('Close the other profile window before exporting legacy storage.'));
    open.onsuccess = () => {
      const db = open.result;
      if (!db.objectStoreNames.contains('library')) { db.close(); reject(new Error('Legacy profile storage has an unknown format.')); return; }
      const tx = db.transaction('library', 'readonly'), request = tx.objectStore('library').get('current');
      tx.oncomplete = () => { db.close(); resolve(request.result?.document ?? null); };
      tx.onabort = tx.onerror = () => { db.close(); reject(tx.error); };
    };
  });
}
export async function captureBrowserProfileInventory(s: State, storage: Pick<Storage, 'getItem'> = localStorage): Promise<BrowserProfileInventory> {
  const issues: string[] = [], raw: Record<string, string | null> = {};
  for (const key of legacyKeys) try { raw[key] = storage.getItem(key); } catch { issues.push(`Cannot read ${key}.`); }
  const array = (key: string): unknown[] => {
    if (!raw[key]) return [];
    try { const parsed: unknown = JSON.parse(raw[key]!); if (Array.isArray(parsed)) return parsed; } catch {  }
    issues.push(`Cannot decode ${key}.`); return [];
  };
  let legacyDocument: ProfileLibraryDocument | null = null;
  try { legacyDocument = await readLegacyDocument(); } catch (e) { issues.push(String(e)); }
  const profiles = new Map<string, ProfileOverlay>();
  const add = (value: unknown) => {
    if (!value || typeof value !== 'object') { issues.push('A legacy profile is not a record.'); return; }
    const p = value as UserPreset;
    if (!['machine', 'process', 'filament'].includes(p.type) || typeof p.name !== 'string' || !(p.overlay ?? p.overrides) || !(p.base?.name ?? p.inherits)) {
      issues.push('A legacy profile is incomplete; its original record is retained in the export.'); return;
    }
    if (typeof p.id !== 'string' || !p.id) {
      issues.push(`Legacy profile ${p.name} needs an explicit identity migration; its original record is retained in the export.`); return;
    }
    const record = storedProfile(normalizeUserPreset(p)), id = record.id!;
    const prior = profiles.get(id);
    if (prior && !equal(prior, record)) issues.push(`Conflicting browser records for ${id}; choose the intended record before adoption.`);
    else profiles.set(id, record);
  };
  s.userPresets.forEach(add);
  array('orca.userPresets').forEach(add);
  array('orca.profileOverlays.v1').forEach(entry => add((entry as {profile?: unknown})?.profile));
  if(legacyDocument && (!Array.isArray(legacyDocument.profiles) || legacyDocument.printers?.schema!==1 || !legacyDocument.printers.setups))issues.push('Legacy library needs recovery; its original document is retained in the export.');
  else legacyDocument?.profiles.forEach(add);
  const catalog = [...array('orca.profileBases.v1'), ...(legacyDocument?.catalog ?? [])] as SystemPreset[];
  const working = structuredClone({printer:s.printer,process:s.process,filamentSlots:s.filamentSlots,slotColours:s.slotColours,filamentEdits:s.filamentEdits,mixes:s.mixes,dirty:s.dirty,projectPresets:s.projectPresets,profileSnapshots:s.profileSnapshots,projectProfileContexts:s.projectProfileContexts});
  return {schema:1,scope:'profile-library-and-working-setup',complete:issues.length===0,storage:raw,legacyDocument,profiles:[...profiles.values()],catalog,printers:structuredClone(capturePrinterMaterials(s)),working,issues};
}
export const browserProfileInventoryText = (inventory: BrowserProfileInventory): string => JSON.stringify(inventory, null, 2);

export interface BrowserProfileAdoptionPlan { document: ProfileLibraryDocument; conflicts: string[] }
export function planBrowserProfileAdoption(remote: ProfileLibraryDocument, inventory: BrowserProfileInventory): BrowserProfileAdoptionPlan {
  const conflicts = [...inventory.issues], profiles = new Map(remote.profiles.map(p => [p.id, p]));
  if(!inventory.complete)conflicts.push('Browser profile/setup inventory is incomplete. Export and recover it before adoption.');
  if(profiles.size!==remote.profiles.length || remote.profiles.some(p=>!p.id))conflicts.push('The saved library contains duplicate or missing identities.');
  for (const p of inventory.profiles) {
    if (!p.id) { conflicts.push(`Missing identity: ${p.name}`); continue; }
    const prior = profiles.get(p.id);
    if (prior && !equal(storedProfile(normalizeUserPreset(prior)), p)) conflicts.push(`Profile ${p.id} differs from its saved record.`);
    else if (!prior) profiles.set(p.id, p);
  }
  const setups = {...remote.printers.setups};
  for (const source of [inventory.legacyDocument?.printers, inventory.printers]) for (const [id, setup] of Object.entries(source?.setups ?? {})) {
    if (setups[id] && !equal(setups[id], setup)) conflicts.push(`Printer setup ${id} differs from its saved setup.`);
    else setups[id] = setup;
  }
  const catalog = [...remote.catalog ?? []];
  for (const base of inventory.catalog) {
    if(!base || !['machine','process','filament'].includes(base.type) || typeof base.name!=='string' || !base.config){conflicts.push('A browser Add source needs recovery.');continue;}
    const prior = catalog.find(p => p.type === base.type && p.vendor === base.vendor && p.name === base.name);
    if (prior && !equal(prior, base)) conflicts.push(`Add source ${base.name} differs from its saved record.`);
    else if (!prior) catalog.push(base);
  }
  for (const [id, setup] of Object.entries(setups)) {
    if (profiles.get(id)?.type!=='machine' || setup.printerId !== id) conflicts.push(`Printer setup ${id} needs identity repair.`);
    if(!Array.isArray(setup.materialIds) || !Array.isArray(setup.slots) || !setup.tuning){conflicts.push(`Printer setup ${id} has an unknown format.`);continue;}
    for (const key of [...setup.materialIds, ...setup.slots.map(slot => slot.materialId), ...Object.keys(setup.tuning)])
      if (profiles.get(key)?.type!=='filament') conflicts.push(`Retained setup reference ${key} needs recovery before adoption.`);
    if(setup.processId && profiles.get(setup.processId)?.type!=='process')conflicts.push(`Retained recipe ${setup.processId} needs recovery before adoption.`);
  }
  return {document:{...remote,profiles:[...profiles.values()],...(catalog.length ? {catalog} : {}),printers:{...remote.printers,setups}},conflicts};
}
export async function adoptBrowserProfileInventory(plan: BrowserProfileAdoptionPlan, operationId: string): Promise<ProfileLibraryDocument> {
  if (plan.conflicts.length) throw Object.assign(new Error(plan.conflicts.join('\n')), {code:'profile_adoption_conflict',recovery:'repair_profile',conflicts:plan.conflicts});
  const result = await persistProfileLibrary({operationId,expectedRevision:plan.document.revision,document:plan.document});
  return {...plan.document,revision:result.revision};
}
