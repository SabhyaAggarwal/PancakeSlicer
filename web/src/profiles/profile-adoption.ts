import type { ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import type { BrowserProfileInventory } from './browser-inventory';
import { catalogKey, normalizeUserPreset, storedProfile } from './model';
import type { PrinterMaterialSetup, ProfileLibraryAdoption, ProfileLibraryAdoptionReceipt, ProfileLibraryDocument } from './ownership';

interface ReadonlySource {
  version: 1;
  complete: true;
  origin: string;
  storage: Record<string,string|null>;
  legacyDocument: ProfileLibraryDocument|null;
}
export interface ProfileAdoptionPlan {
  document: ProfileLibraryDocument;
  conflicts: string[];
  adoption: ProfileLibraryAdoption;
  operationId: string;
  alreadyAcknowledged: boolean;
}
export type BrowserProfileAdoptionPlan = ProfileAdoptionPlan;
const object=(v:unknown):v is Record<string,unknown>=>!!v && typeof v==='object' && !Array.isArray(v);
const canonical=(value:unknown):unknown=>Array.isArray(value)?value.map(canonical):object(value)?Object.fromEntries(Object.keys(value).sort().filter(k=>value[k]!==undefined).map(k=>[k,canonical(value[k])])):value;
const text=(value:unknown)=>JSON.stringify(canonical(value));
const equal=(a:unknown,b:unknown)=>text(a)===text(b);
export { equal as profileAdoptionValuesEqual };
function sourceOf(inventory:BrowserProfileInventory,origin:string):ReadonlySource{
  if(!inventory.complete || inventory.issues.length)throw new Error('Browser profile inventory is incomplete. Keep the original sources for recovery.');
  if(!origin)throw new Error('The browser source origin is unavailable.');
  const source:ReadonlySource={version:1,complete:true,origin,storage:inventory.storage,legacyDocument:inventory.legacyDocument};
  entries(source);return source;
}
export async function fingerprintBrowserProfileInventory(inventory:BrowserProfileInventory,origin:string):Promise<ProfileLibraryAdoption>{
  const source=text(sourceOf(inventory,origin));
  const digest=[...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(source)))].map(n=>n.toString(16).padStart(2,'0')).join('');
  return {scope:'profile-library-and-working-setup',sourceId:`browser-profile-library:v1:${origin}`,digest,source};
}
function decodeSource(source:string):ReadonlySource{
  const value:unknown=JSON.parse(source);
  if(!object(value) || value.version!==1 || value.complete!==true || typeof value.origin!=='string' || !object(value.storage)
    || Object.values(value.storage).some(v=>v!==null && typeof v!=='string'))throw new Error('The acknowledged browser source is invalid or incomplete.');
  return value as unknown as ReadonlySource;
}
export type ProfileAdoptionEntry={path:string;kind:'profile'|'setup'|'catalog';key:string;value:unknown};
type Entry=ProfileAdoptionEntry;
export type ProfileAdoptionBaselines=ReadonlyMap<string,readonly {entry:ProfileAdoptionEntry;revision:number}[]>;
function entries(source:ReadonlySource):Entry[]{
  const result:Entry[]=[];
  const profiles=(values:unknown,path:string)=>{
    if(!Array.isArray(values))throw new Error(`Invalid legacy profile array: ${path}`);
    const ids=new Set<string>();
    for(const value of values){
      if(!object(value) || typeof value.id!=='string' || !value.id || ids.has(value.id))throw new Error(`Missing or duplicate legacy identity: ${path}`);
      if(!['machine','process','filament'].includes(String(value.type)) || typeof value.name!=='string' || !object(value.overlay ?? value.overrides)
        || !(object(value.base) && typeof value.base.name==='string') && typeof value.inherits!=='string')throw new Error(`Incomplete legacy profile: ${path}/${value.id}`);
      ids.add(value.id);result.push({path:`${path}/${value.id}`,kind:'profile',key:value.id,value});
    }
  };
  const catalogs=(values:unknown,path:string)=>{
    if(!Array.isArray(values))throw new Error(`Invalid legacy Add sources: ${path}`);
    const keys=new Set<string>();
    for(const value of values){
      if(!object(value) || typeof value.name!=='string' || typeof value.vendor!=='string' || !object(value.config) || !['machine','process','filament'].includes(String(value.type)))throw new Error('A legacy Add source needs repair.');
      const key=catalogKey(value as unknown as SystemPreset);if(keys.has(key))throw new Error('Legacy Add source identities are ambiguous.');keys.add(key);result.push({path:`${path}/${key}`,kind:'catalog',key,value});
    }
  };
  const parse=(key:string)=>JSON.parse(source.storage[key] ?? '[]');
  profiles(parse('orca.userPresets'),'storage/orca.userPresets');
  const overlays=parse('orca.profileOverlays.v1');
  if(!Array.isArray(overlays))throw new Error('Invalid legacy overlay array.');
  profiles(overlays.map(value=>value?.profile),'storage/orca.profileOverlays.v1');
  catalogs(parse('orca.profileBases.v1'),'storage/orca.profileBases.v1');
  const doc=source.legacyDocument;
  if(doc){
    if(doc.schema!==1 || doc.printers?.schema!==1 || !object(doc.printers.setups))throw new Error('The readonly legacy library needs repair.');
    profiles(doc.profiles,'database/library/profiles');catalogs(doc.catalog ?? [],'database/library/catalog');
    for(const [id,setup] of Object.entries(doc.printers.setups))result.push({path:`database/library/setups/${id}`,kind:'setup',key:id,value:setup});
  }
  return result;
}
function normalized(entry:Entry):unknown{
  if(entry.kind!=='profile')return entry.value;
  return storedProfile(normalizeUserPreset(entry.value as UserPreset));
}
function mergeDelta(before:unknown,after:unknown,current:unknown,path:string,conflicts:string[]):unknown{
  if(equal(before,after))return current;
  if(equal(current,before) || equal(current,after))return structuredClone(after);
  if(object(before) && object(after) && object(current)){
    const next={...current};
    for(const key of new Set([...Object.keys(before),...Object.keys(after)])){
      const value=mergeDelta(before[key],after[key],current[key],`${path}.${key}`,conflicts);
      if(value===undefined)delete next[key];else Object.defineProperty(next,key,{value,writable:true,enumerable:true,configurable:true});
    }
    return next;
  }
  conflicts.push(`${path} changed in both the retained source and the saved library.`);return current;
}
export function planBrowserProfileAdoption(remote:ProfileLibraryDocument,inventory:BrowserProfileInventory,adoption:ProfileLibraryAdoption,receipts:readonly ProfileLibraryAdoptionReceipt[]):BrowserProfileAdoptionPlan{
  const conflicts=[...inventory.issues],operationId=`browser-adoption:${adoption.digest}`;
  const result=(document:ProfileLibraryDocument,alreadyAcknowledged=false):BrowserProfileAdoptionPlan=>({document,conflicts,adoption,operationId,alreadyAcknowledged});
  let source:ReadonlySource,currentEntries:Entry[];
  try{
    source=decodeSource(adoption.source);
    if(adoption.identityResolutions !== undefined || adoption.scope!=='profile-library-and-working-setup' || adoption.sourceId!==`browser-profile-library:v1:${source.origin}` || !/^[a-f0-9]{64}$/.test(adoption.digest)
      || adoption.source!==text(sourceOf(inventory,source.origin)))throw new Error('The adoption fingerprint no longer matches this complete readonly capture.');
    currentEntries=entries(source);
  }catch(error){conflicts.push(String(error));return result(remote);}
  const baselines=new Map<string,{entry:Entry;revision:number}[]>();
  for(const receipt of receipts){
    if(receipt.sourceId!==adoption.sourceId)continue;
    if(receipt.identityResolutions !== undefined || receipt.scope!==adoption.scope || receipt.durable!==true || !Number.isInteger(receipt.revision) || receipt.operationId!==`browser-adoption:${receipt.digest}`){conflicts.push('A service adoption receipt needs recovery.');continue;}
    try{
      const acknowledged=decodeSource(receipt.source);
      if(acknowledged.origin!==source.origin)throw new Error('The adoption receipt origin does not match.');
      if(receipt.digest===adoption.digest){if(receipt.source!==adoption.source)throw new Error('The adoption digest has different source bytes.');return result(remote,true);}
      for(const entry of entries(acknowledged)){const prior=baselines.get(entry.path) ?? [];prior.push({entry,revision:receipt.revision});baselines.set(entry.path,prior);}
    }catch(error){conflicts.push(String(error));}
  }
  return result(mergeProfileAdoptionEntries(remote,currentEntries,baselines,conflicts));
}

export function mergeProfileAdoptionEntries(remote:ProfileLibraryDocument,currentEntries:readonly ProfileAdoptionEntry[],baselines:ProfileAdoptionBaselines,conflicts:string[]):ProfileLibraryDocument{
  const profiles=new Map(remote.profiles.map(p=>[p.id,p])),setups={...remote.printers.setups},catalog=new Map((remote.catalog ?? []).map(p=>[catalogKey(p),p]));
  if(profiles.size!==remote.profiles.length || remote.profiles.some(p=>!p.id)){conflicts.push('The saved library has duplicate or missing identities.');return remote;}
  if(catalog.size!==(remote.catalog ?? []).length){conflicts.push('The saved Add sources have duplicate exact identities.');return remote;}
  for(const entry of currentEntries){
    const prior=[...(baselines.get(entry.path) ?? [])].sort((a,b)=>b.revision-a.revision);
    if(prior.some(p=>equal(p.entry.value,entry.value)))continue;
    const map=entry.kind==='profile'?profiles:entry.kind==='catalog'?catalog:undefined;
    const existing=map?map.get(entry.key):setups[entry.key],after=normalized(entry),baseline=prior[0] && normalized(prior[0].entry);
    if(prior.length && existing===undefined){conflicts.push(`${entry.path} was removed from the service. Recover an explicit copy instead of resurrecting it.`);continue;}
    const current=entry.kind==='profile' && existing?storedProfile(normalizeUserPreset(existing as UserPreset)):existing;
    let merged:unknown;
    if(baseline)merged=mergeDelta(baseline,after,current,entry.path,conflicts);
    else if(existing===undefined || equal(current,after))merged=after;
    else{conflicts.push(`${entry.path} conflicts with an existing saved identity.`);continue;}
    if(equal(current,merged))continue;
    if(entry.kind==='profile')profiles.set(entry.key,{...merged as ProfileOverlay,revision:existing?Math.max((existing as ProfileOverlay).revision ?? 0,(merged as ProfileOverlay).revision ?? 0)+1:(merged as ProfileOverlay).revision ?? 0});
    else if(entry.kind==='catalog')catalog.set(entry.key,merged as SystemPreset);
    else setups[entry.key]={...merged as PrinterMaterialSetup,revision:existing?Math.max((existing as PrinterMaterialSetup).revision,(merged as PrinterMaterialSetup).revision)+1:(merged as PrinterMaterialSetup).revision};
  }
  for(const [id,setup] of Object.entries(setups)){
    if(profiles.get(id)?.type!=='machine' || setup.printerId!==id || !Array.isArray(setup.materialIds) || !Array.isArray(setup.slots) || !setup.tuning){conflicts.push(`Printer setup ${id} needs identity recovery.`);continue;}
    for(const key of [...setup.materialIds,...setup.slots.map(slot=>slot.materialId),...Object.keys(setup.tuning)])if(profiles.get(key)?.type!=='filament')conflicts.push(`Retained material ${key} needs recovery.`);
    if(setup.processId && profiles.get(setup.processId)?.type!=='process')conflicts.push(`Retained print settings ${setup.processId} need recovery.`);
  }
  return ({...remote,profiles:[...profiles.values()],...(catalog.size?{catalog:[...catalog.values()]}:{}),printers:{...remote.printers,revision:equal(setups,remote.printers.setups)?remote.printers.revision:remote.printers.revision+1,setups}});
}
