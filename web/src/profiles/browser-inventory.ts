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
