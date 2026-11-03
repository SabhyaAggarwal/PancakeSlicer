import type { ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import type { State } from '../state/store';
import { persistProfileLibrary, readProfileLibrary } from '../persist/profile-library';
import { normalizeUserPreset, storedProfile } from './model';
import { capturePrinterMaterials } from './material-library';
import type { PrinterProfileLibrary, ProfileLibraryDocument } from './ownership';
import type { BrowserProfileAdoptionPlan } from './profile-adoption';
export { planBrowserProfileAdoption, fingerprintBrowserProfileInventory } from './profile-adoption';
export type { BrowserProfileAdoptionPlan } from './profile-adoption';

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
const record = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v)
  && (Object.getPrototypeOf(v) === Object.prototype || Object.getPrototypeOf(v) === null);
const revision = (v: unknown) => Number.isSafeInteger(v) && Number(v) >= 0;
function jsonData(value: unknown, seen = new Set<object>()): boolean {
  if (value === null || typeof value === 'string' || typeof value === 'boolean') return true;
  if (typeof value === 'number') return Number.isFinite(value);
  if (!value || typeof value !== 'object' || seen.has(value)) return false;
  if (!Array.isArray(value) && Object.getPrototypeOf(value) !== Object.prototype && Object.getPrototypeOf(value) !== null) return false;
  seen.add(value);
  const valid = Array.isArray(value)
    ? Object.keys(value).length === value.length && Array.from(value).every(v => jsonData(v, seen))
    : Object.values(value).every(v => jsonData(v, seen));
  seen.delete(value);
  return valid;
}
function knownDocument(value: unknown): value is ProfileLibraryDocument {
  if (!record(value) || !jsonData(value) || value.schema !== 1 || !revision(value.revision) || !Array.isArray(value.profiles)
    || !record(value.printers) || value.printers.schema !== 1 || !revision(value.printers.revision) || !record(value.printers.setups)
    || value.catalog !== undefined && !Array.isArray(value.catalog)) return false;
  return value.profiles.every(p => record(p) && ['machine','process','filament'].includes(String(p.type)) && typeof p.name === 'string'
    && record(p.overlay ?? p.overrides) && (record(p.base) && typeof p.base.name === 'string' || typeof p.inherits === 'string'))
    && (value.catalog ?? []).every(p => record(p) && typeof p.name === 'string' && typeof p.vendor === 'string'
      && ['machine','process','filament'].includes(String(p.type)) && record(p.config))
    && Object.entries(value.printers.setups).every(([id, setup]) => record(setup) && setup.schema === 1 && setup.printerId === id
      && revision(setup.revision) && Array.isArray(setup.materialIds) && setup.materialIds.every(id => typeof id === 'string')
      && Array.isArray(setup.slots) && setup.slots.every(slot => record(slot) && typeof slot.id === 'string' && typeof slot.materialId === 'string')
      && record(setup.assignments) && Object.values(setup.assignments).every(id => typeof id === 'string')
      && record(setup.tuning) && Object.values(setup.tuning).every(record) && record(setup.mixes));
}
function knownEnvelope(value: unknown): ProfileLibraryDocument | null {
  if (!record(value) || Object.keys(value).length !== 2 || !Object.hasOwn(value,'document') || !Object.hasOwn(value,'receipts')
    || value.document !== null && !knownDocument(value.document) || !record(value.receipts)
    || Object.values(value.receipts).some(receipt => !record(receipt) || Object.keys(receipt).length !== 2
      || !Object.hasOwn(receipt,'payload') || !Object.hasOwn(receipt,'revision') || typeof receipt.payload !== 'string' || !revision(receipt.revision)))
    throw new Error('The present legacy profile envelope is not a complete known format. Retain the original database and recover it before adoption.');
  return value.document as ProfileLibraryDocument | null;
}
async function readLegacyDocument(): Promise<ProfileLibraryDocument | null> {
  if (typeof indexedDB === 'undefined') throw new Error('Browser profile storage is unavailable; its absence has not been established.');
  if (!indexedDB.databases) throw new Error('This browser cannot enumerate legacy profile storage. Export it from the previous app before adoption.');
  const listed = (await indexedDB.databases()).find(db => db.name === 'orca-profile-library');
  if (!listed) return null;
  return new Promise((resolve, reject) => {
    let settled = false, db: IDBDatabase | undefined, tx: IDBTransaction | undefined;
    const fail = (error: unknown) => {
      if (settled) return;
      settled = true;
      try { tx?.abort(); } catch {  }
      db?.close(); reject(error ?? new Error('Legacy profile inventory could not be read. Retain the original database.'));
    };
    const done = (document: ProfileLibraryDocument | null) => {
      if (settled) return;
      settled = true; db?.close(); resolve(document);
    };
    const open = indexedDB.open('orca-profile-library');
    open.onupgradeneeded = () => {
      try { open.transaction?.abort(); } catch {  }
      open.result.close();
      fail(new Error('Legacy profile storage changed during capture. No database was created or upgraded; retain the originals and retry.'));
    };
    open.onerror = () => fail(open.error);
    open.onblocked = () => fail(new Error('Close the other profile window before recovering legacy storage.'));
    open.onsuccess = () => {
      if (settled) { open.result.close(); return; }
      db = open.result;
      db.onversionchange = () => { db!.close(); fail(new Error('Legacy profile storage changed version during capture. Retain it and retry.')); };
      db.onclose = () => fail(new Error('Legacy profile storage closed before capture completed. Retain it and retry.'));
      try {
        if (listed.version !== undefined && listed.version !== db.version) throw new Error('Legacy profile storage changed after enumeration. Retain it and retry.');
        const names = Array.from(db.objectStoreNames);
        if (!names.length) throw new Error('Legacy profile storage has an unknown schema. Retain it for recovery.');
        tx = db.transaction(names, 'readonly');
        const stores = names.map(name => {
          const store = tx!.objectStore(name), keys = store.getAllKeys();
          keys.onerror = () => fail(keys.error);
          return {name,keys,known:store.keyPath === null && !store.autoIncrement && store.indexNames.length === 0};
        });
        const current = names.includes('library') ? tx.objectStore('library').get('current') : undefined;
        if (current) current.onerror = () => fail(current.error);
        tx.onabort = tx.onerror = () => fail(tx?.error);
        tx.oncomplete = () => {
          if (settled) return;
          try {
            if (stores.length !== 1 || stores[0].name !== 'library' || !stores[0].known || !current)
              throw new Error('Legacy profile storage has unknown stores or schema. Retain the complete database for recovery before adoption.');
            const keys = stores[0].keys.result;
            if (keys.length === 0 && current.result === undefined) { done(null); return; }
            if (keys.length !== 1 || keys[0] !== 'current') throw new Error('Legacy profile storage contains unrecognized records. Retain the complete database for recovery before adoption.');
            done(knownEnvelope(current.result));
          } catch (error) { fail(error); }
        };
      } catch (error) { fail(error); }
    };
  });
}
export async function captureBrowserProfileInventory(s: State, storage: Pick<Storage, 'getItem'> = localStorage): Promise<BrowserProfileInventory> {
  const issues: string[] = [], raw: Record<string, string | null> = {};
  for (const key of legacyKeys) try { raw[key] = storage.getItem(key); } catch { issues.push(`Cannot read ${key}.`); }
  const array = (key: string): unknown[] => {
    if (raw[key] === null || raw[key] === undefined) return [];
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
    if (!profiles.has(id)) profiles.set(id, record);
  };
  array('orca.userPresets').forEach(add);
  array('orca.profileOverlays.v1').forEach(entry => add((entry as {profile?: unknown})?.profile));
  if(legacyDocument && (!Array.isArray(legacyDocument.profiles) || legacyDocument.printers?.schema!==1 || !legacyDocument.printers.setups))issues.push('Legacy library needs recovery; its original document is retained in the export.');
  else legacyDocument?.profiles.forEach(add);
  const catalog = [...array('orca.profileBases.v1'), ...(legacyDocument?.catalog ?? [])] as SystemPreset[];
  const working = structuredClone({printer:s.printer,process:s.process,filamentSlots:s.filamentSlots,slotColours:s.slotColours,filamentEdits:s.filamentEdits,mixes:s.mixes,dirty:s.dirty,projectPresets:s.projectPresets,profileSnapshots:s.profileSnapshots,projectProfileContexts:s.projectProfileContexts});
  return {schema:1,scope:'profile-library-and-working-setup',complete:issues.length===0,storage:raw,legacyDocument,profiles:[...profiles.values()],catalog,printers:structuredClone(capturePrinterMaterials(s)),working,issues};
}
export const browserProfileInventoryText = (inventory: BrowserProfileInventory): string => JSON.stringify(inventory, null, 2);

export async function adoptBrowserProfileInventory(plan: BrowserProfileAdoptionPlan): Promise<ProfileLibraryDocument> {
  if (plan.conflicts.length) throw Object.assign(new Error(plan.conflicts.join('\n')), {code:'profile_adoption_conflict',recovery:'repair_profile',conflicts:plan.conflicts});
  if(!plan.alreadyAcknowledged)await persistProfileLibrary({operationId:plan.operationId,expectedRevision:plan.document.revision,document:plan.document,adoption:plan.adoption});
  const latest=await readProfileLibrary();
  if(!latest)throw new Error('The required profile-library API is unavailable. Keep this adoption receipt and retry.');
  return latest;
}
