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
export interface RendererProfileOrigin {
  origin: string;
  coverage: {localStorage:'decoded'|'verified-absent'|'incomplete';indexedDB:'decoded'|'verified-absent'|'incomplete'};
  localStorage: readonly {key:string;value:string}[];
  databases: readonly {name:string;version:number;metadata?:Record<string,unknown>;stores:readonly {
    name:string;metadata?:Record<string,unknown>;records:readonly {key:IDBValidKey;value:unknown}[];
  }[]}[];
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
function knownStores(stores:readonly {name:string;keys:readonly unknown[];known:boolean}[],current:unknown):ProfileLibraryDocument|null{
  if(stores.length!==1 || stores[0].name!=='library' || !stores[0].known)
    throw new Error('Legacy profile storage has unknown stores or schema. Retain the complete database for recovery before adoption.');
  const keys=stores[0].keys;
  if(!keys.length && current===undefined)return null;
  if(keys.length!==1 || keys[0]!=='current')throw new Error('Legacy profile storage contains unrecognized records. Retain the complete database for recovery before adoption.');
  return knownEnvelope(current);
}
const emptyMetadata=(value:unknown)=>value===undefined || record(value) && !Object.keys(value).length;
const metadataFields=(value:unknown,required:readonly string[],optional:readonly string[]):value is Record<string,unknown>=>record(value) && jsonData(value)
  && required.every(key=>Object.hasOwn(value,key)) && Object.keys(value).every(key=>required.includes(key)||optional.includes(key));
function metadataInteger(value:unknown):bigint|undefined{
  if(typeof value==='number' && revision(value) && !Object.is(value,-0))return BigInt(value);
  if(typeof value==='string' && /^[1-9][0-9]{15,18}$/.test(value)){
    const n=BigInt(value);if(n>BigInt(Number.MAX_SAFE_INTEGER) && n<(1n<<63n))return n;
  }
}
function metadataIds(value:unknown,min:bigint,max:bigint,live:bigint):boolean{
  if(value===undefined)return true;
  if(!Array.isArray(value))return false;
  const ids=value.map(metadataInteger);
  return new Set(ids).size===ids.length && ids.every(id=>id!==undefined && id>=min && id<=max && id!==live);
}
function knownRendererProfileSchema(db:Record<string,unknown>,origin:string):boolean{
  if(!Array.isArray(db.stores))return false;
  if(emptyMetadata(db.metadata))return db.stores.every(store=>record(store) && emptyMetadata(store.metadata));
  const m=db.metadata;
  if(db.version!==1 || !metadataFields(m,['id','originIdentifier','global'],[
    'originName','databaseName','userStringVersion','maxObjectStoreId','blobNumberGenerator','freeObjectStoreIds']))return false;
  const id=metadataInteger(m.id),g=m.global,identifier=/^http_127\.0\.0\.1_([1-9][0-9]{0,4})@1$/.exec(String(m.originIdentifier));
  if(id===undefined || id===0n || typeof m.originIdentifier!=='string' || !identifier
    || Number(identifier[1])>65535 || `http://127.0.0.1:${identifier[1]}`!==origin
    || m.originName!==undefined && m.originName!==m.originIdentifier
    || m.databaseName!==undefined && m.databaseName!==db.name
    || m.userStringVersion!==undefined && typeof m.userStringVersion!=='string'
    || !metadataFields(g,['schemaVersion','dataVersion','maxDatabaseId'],[
      'scopesVersion','freeDatabaseIds','recoveryBlobJournal','activeBlobJournal','earliestSweepTime','earliestCompactionTime']))return false;
  const maxDb=metadataInteger(g.maxDatabaseId),format=metadataInteger(g.dataVersion);
  if(![4,5].includes(Number(g.schemaVersion)) || typeof g.schemaVersion!=='number' || maxDb===undefined || id>maxDb || format===undefined
    || ![15n,16n].includes(format>>32n) || (format&0xffffffffn)<16n || (format&0xffffffffn)>21n
    || g.scopesVersion!==undefined && g.scopesVersion!==1
    || !metadataIds(g.freeDatabaseIds,1n,maxDb,id))return false;
  for(const field of ['recoveryBlobJournal','activeBlobJournal'])if(g[field]!==undefined && (!Array.isArray(g[field]) || g[field].length))return false;
  for(const field of ['earliestSweepTime','earliestCompactionTime'])if(g[field]!==undefined && metadataInteger(g[field])===undefined)return false;
  for(const field of ['maxObjectStoreId','blobNumberGenerator'])if(m[field]!==undefined && metadataInteger(m[field])===undefined)return false;
  const maxStore=metadataInteger(m.maxObjectStoreId)??((1n<<63n)-1n),storeIds=new Set<bigint>();
  for(const store of db.stores){
    if(!record(store))return false;
    const s=store.metadata;
    if(!metadataFields(s,['id','keyPath','autoIncrement'],['hasKeyPath','evictable','lastVersion','maxIndexId','keyGeneratorCurrentNumber',
      'indexes','freeIndexIds','externalObjectRecords','orphanExternalObjects','indexEntries']))return false;
    const sid=metadataInteger(s.id);
    if(sid===undefined || sid===0n || sid>maxStore || storeIds.has(sid) || !metadataIds(m.freeObjectStoreIds,1n,maxStore,sid)
      || s.autoIncrement!==false || s.hasKeyPath!==undefined && s.hasKeyPath!==false
      || !(s.keyPath===null || s.keyPath==='' && s.hasKeyPath===false)
      || s.evictable!==undefined && typeof s.evictable!=='boolean')return false;
    storeIds.add(sid);
    for(const field of ['lastVersion','maxIndexId','keyGeneratorCurrentNumber'])if(s[field]!==undefined && metadataInteger(s[field])===undefined)return false;
    for(const field of ['indexes','externalObjectRecords','orphanExternalObjects','indexEntries'])if(s[field]!==undefined && (!Array.isArray(s[field]) || s[field].length))return false;
    if(!metadataIds(s.freeIndexIds,30n,metadataInteger(s.maxIndexId)??((1n<<63n)-1n),0n))return false;
  }
  return true;
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
            done(knownStores(stores.map(store=>({...store,keys:store.keys.result})),current?.result));
          } catch (error) { fail(error); }
        };
      } catch (error) { fail(error); }
    };
  });
}
export async function captureBrowserProfileInventory(s: State, storage: Pick<Storage, 'getItem'> = localStorage): Promise<BrowserProfileInventory> {
  const issues: string[] = [], raw: Record<string, string | null> = {};
  for (const key of legacyKeys) try { raw[key] = storage.getItem(key); } catch { issues.push(`Cannot read ${key}.`); }
  let legacyDocument: ProfileLibraryDocument | null = null;
  try { legacyDocument = await readLegacyDocument(); } catch (e) { issues.push(String(e)); }
  return profileInventory(s,raw,legacyDocument,issues);
}
export function captureRendererProfileInventory(s:State,origin:RendererProfileOrigin):BrowserProfileInventory{
  const issues:string[]=[],raw:Record<string,string|null>=Object.fromEntries(legacyKeys.map(key=>[key,null]));
  let legacyDocument:ProfileLibraryDocument|null=null;
  try{
    const url=new URL(origin.origin);
    if(url.protocol!=='http:' || url.hostname!=='127.0.0.1' || url.origin!==origin.origin || !origin.coverage
      || !['decoded','verified-absent'].includes(origin.coverage.localStorage) || !['decoded','verified-absent'].includes(origin.coverage.indexedDB)
      || !Array.isArray(origin.localStorage) || !Array.isArray(origin.databases)
      || origin.coverage.localStorage==='verified-absent' && origin.localStorage.length
      || origin.coverage.indexedDB==='verified-absent' && origin.databases.length)throw new Error('Original renderer profile coverage is incomplete. Retain the host source and retry its verified capture.');
    const keys=new Set<string>();
    for(const row of origin.localStorage){
      if(!record(row) || Object.keys(row).length!==2 || typeof row.key!=='string' || typeof row.value!=='string' || keys.has(row.key))throw new Error('Original renderer storage has invalid or duplicated keys.');
      keys.add(row.key);if(Object.hasOwn(raw,row.key))raw[row.key]=row.value;
    }
    const names=new Set<string>();
    for(const db of origin.databases){
      if(!record(db) || typeof db.name!=='string' || !db.name || names.has(db.name))throw new Error('Original renderer databases are not completely identified.');
      names.add(db.name);if(db.name!=='orca-profile-library')continue;
      if(!revision(db.version) || !db.version || !Array.isArray(db.stores)
        || Object.keys(db).some(key=>!['name','version','stores','metadata'].includes(key)))throw new Error('Original profile database has unknown schema metadata. Retain it for recovery.');
      let current:unknown;
      const stores=db.stores.map(store=>{
        if(!record(store) || typeof store.name!=='string' || !Array.isArray(store.records)
          || Object.keys(store).some(key=>!['name','records','metadata'].includes(key)))throw new Error('Original profile store inventory is incomplete.');
        for(const row of store.records){
          if(!record(row) || Object.keys(row).length!==2 || !Object.hasOwn(row,'key') || !Object.hasOwn(row,'value'))throw new Error('Original profile store contains an unrecognized captured record.');
          if(store.name==='library' && row.key==='current')current=row.value;
        }
        return {name:store.name,keys:store.records.map(row=>row.key),known:true};
      });
      if(!knownRendererProfileSchema(db,origin.origin))throw new Error('Original profile database has unknown physical schema or pending state. Retain the full host inventory for recovery.');
      legacyDocument=knownStores(stores,current);
    }
  }catch(error){issues.push(error instanceof Error?error.message:String(error));}
  return profileInventory(s,raw,legacyDocument,issues);
}
function profileInventory(s:State,raw:Record<string,string|null>,legacyDocument:ProfileLibraryDocument|null,issues:string[]):BrowserProfileInventory{
  const array = (key: string): unknown[] => {
    if (raw[key] === null || raw[key] === undefined) return [];
    try { const parsed: unknown = JSON.parse(raw[key]!); if (Array.isArray(parsed)) return parsed; } catch {  }
    issues.push(`Cannot decode ${key}.`); return [];
  };
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
