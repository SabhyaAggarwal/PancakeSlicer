import type { PresetType, SystemPreset, UserPreset } from '../api/types';
import { getState, markStale, setState, type State } from '../state/store';
import { readProfileLibrary, persistProfileLibrary } from '../persist/profile-library';
import { emptyPrinterLibrary, type PrinterProfileLibrary, type ProfileLibraryWrite } from './ownership';
import { findProfile, profileKey } from './identity';
import { catalogKey, findCatalogBase, normalizeUserPreset, prepareProfile, storedProfile } from './model';
import { editableMaster, archiveProfile, restoreArchivedProfile } from './library-actions';
import { randomId } from '../random-id';
let tail:Promise<unknown>=Promise.resolve();
const serialize = <T>(action:()=>Promise<T>):Promise<T> => {const result=tail.then(action);tail=result.catch(()=>{});return result;};
export const serializeProfileWrite=serialize;
const ordered=(value:unknown):unknown=>Array.isArray(value)?value.map(ordered):value && typeof value==='object'?Object.fromEntries(Object.entries(value).sort(([a],[b])=>a.localeCompare(b)).filter(([,v])=>v!==undefined).map(([k,v])=>[k,ordered(v)])):value;
const equal=(a:unknown,b:unknown)=>JSON.stringify(ordered(a))===JSON.stringify(ordered(b));
const sameSetups=(a:PrinterProfileLibrary|undefined,b:PrinterProfileLibrary|undefined)=>equal(a?.setups ?? {},b?.setups ?? {});
const stored=(p:UserPreset)=>{
  const {snapshot:_s,baseline:_b,kind:_k,aliases:_a,provenance:_p,masterId:_m,printerId:_pr,parent:_pa,metadata,base,name,...rest}=storedProfile(normalizeUserPreset(p)) as ReturnType<typeof storedProfile>&Record<string,unknown>;
  const family=(metadata as Record<string,unknown>|undefined)?.family;
  return {...rest,name:String(name).replace(/\s+@.*$/,'').trim() || name,base:{name:base?.name,vendor:base?.vendor},...(family ? {family} : {})};
};
const conflict=(message:string)=>Object.assign(new Error(message),{code:'revision_conflict',recovery:'reload_profiles'});
interface FrozenWrite {write:ProfileLibraryWrite;records:UserPreset[];origins:Map<string,UserPreset|undefined>;signature:string;retryable:boolean;acknowledged?:boolean;setupBaseline?:PrinterProfileLibrary}
interface Acknowledgement {request:FrozenWrite;document:import('./ownership').ProfileLibraryDocument;records:UserPreset[];changed:Set<string>;setupsChanged?:boolean}
let pending:FrozenWrite|undefined;
const ownAcknowledgements=new WeakMap<UserPreset,UserPreset>();
const ownSetupAcknowledgements=new WeakMap<PrinterProfileLibrary,PrinterProfileLibrary>();
export interface PrinterSetupSaveContext {baseline:PrinterProfileLibrary;isCurrent():boolean}
export function profileOriginIsCurrent(current:UserPreset|undefined,origin:UserPreset|undefined):boolean{
  return current===origin || !!current && !!origin && !!ownAcknowledgements.get(origin) && equal(stored(current),stored(ownAcknowledgements.get(origin)!));
}
const signature=(records:UserPreset[],printers?:PrinterProfileLibrary,catalog?:SystemPreset[])=>JSON.stringify(ordered({records:records.map(stored),printers,catalog}));
const ready=(s:State)=>{if(s.profileLibraryLoad.state!=='ready')throw new Error(s.profileLibraryLoad.state==='failed'?s.profileLibraryLoad.error:'Hang tight—waiting for your profile library to finish loading (your edits are safe!).');};
function freezeWrite(s:State,write:ProfileLibraryWrite,records:UserPreset[],requestSignature:string,setupBaseline?:PrinterProfileLibrary):FrozenWrite{
  return {write:structuredClone(write),records,signature:requestSignature,retryable:true,setupBaseline,origins:new Map(records.map(p=>[profileKey(p),findProfile(s,p.type,profileKey(p))]))};
}
async function acknowledge(request:FrozenWrite):Promise<Acknowledgement>{
  pending=request;
  try{await persistProfileLibrary(request.write);}
  catch(error){
    const failure=error as {status?:number;code?:string};
    if(failure.code==='revision_conflict' || failure.code==='library_reset' || [400,401,403,404,422].includes(failure.status ?? 0)){request.retryable=false;if(pending===request)pending=undefined;}
    throw error;
  }
  const document=await readProfileLibrary(),records:UserPreset[]=[],changed=new Set<string>();
  for(const submitted of request.records){
    const raw=document.profiles.find(p=>p.id===profileKey(submitted));
    if(!raw){changed.add(profileKey(submitted));continue;}
    const record=normalizeUserPreset(raw);records.push(record);
    const sent=request.write.document.profiles.find(p=>p.id===profileKey(submitted));
    if(!sent || !equal(stored(record),stored(sent as UserPreset)))changed.add(profileKey(submitted));
    else {const origin=request.origins.get(profileKey(submitted));if(origin)ownAcknowledgements.set(origin,record);}
  }
  const setupsChanged=!!request.setupBaseline && !sameSetups(document.printers,request.write.document.printers);
  if(request.setupBaseline && !setupsChanged)ownSetupAcknowledgements.set(request.setupBaseline,document.printers);
  request.acknowledged=true;request.retryable=false;if(pending===request)pending=undefined;
  return {request,document,records,changed,setupsChanged};
}
function publishAcknowledgement(ack:Acknowledgement,apply?:(s:State)=>Partial<State>){
  const ids=new Set(ack.records.map(profileKey));
  if(ids.size)void import('./catalog-startup').then(m=>m.ensureLibraryPresets());
  setState(s=>({...apply?.(s),profileLibraryRevision:Math.max(s.profileLibraryRevision,ack.document.revision),job:markStale(s.job),
    userPresets:!ids.size?s.userPresets:[...s.userPresets.filter(p=>!ids.has(profileKey(p))),...ack.records.map(p=>{const current=findProfile(s,p.type,profileKey(p));return current && ((current.revision ?? 0)>(p.revision ?? 0) || equal(stored(current),stored(p)))?current:p;})]}));
}
async function settleOutstanding():Promise<Acknowledgement|undefined>{
  if(!pending)return;
  const ack=await acknowledge(pending);publishAcknowledgement(ack);return ack;
}
export function acknowledgePendingProfileWrite():Promise<void>{
  return serialize(async()=>{if(pending)await acknowledge(pending);});
}
export interface ProfileCreation {commit():Promise<string>; readonly retryable:boolean}
export interface CreationPlan {records:UserPreset[];reused?:string[];skipped?:string[]}
export interface CreationResult {keys:string[];skipped:string[]}
export interface ProfileBatchCreation {commit():Promise<CreationResult>;readonly retryable:boolean}
export function profileBatchCreation(factory:(s:State)=>CreationPlan):ProfileBatchCreation{
  let request:FrozenWrite|undefined,keys:string[]=[],reused:string[]=[],skipped:string[]=[],flight:Promise<CreationResult>|undefined,service:State['api'];
  return {get retryable(){return !!request && (request.retryable || !!request.acknowledged);},commit:()=>flight ??= serialize(async()=>{
    ready(getState());
    if(request && getState().api!==service)throw conflict('Your profile library updated in the background. We kept your new profile draft—click Save again to finish adding it.');
    if(pending && pending!==request)await settleOutstanding();
    if(!request){
      const initial=getState();ready(initial);
      const remote=await readProfileLibrary(initial.api?.mock),latest=getState();ready(latest);
      if(latest.api!==initial.api)throw new Error('The profile library updated while you were adding that. Your current setup is safe—try adding it again.');
      const records=remote.profiles.map(normalizeUserPreset);
      if(records.some(p=>!p.id) || new Set(records.map(profileKey)).size!==records.length)throw new Error('Please finish recovering your saved profiles before adding new ones.');
      const canonical={...latest,userPresets:records},plan=factory(canonical);
      reused=plan.reused ?? [];skipped=plan.skipped ?? [];
      const existing=reused.map(key=>{const record=records.find(p=>profileKey(p)===key);if(!record)throw new Error('Oops! That shared profile isn\'t available right now.');return record;});
      if(existing.length)setState(s=>({profileLibraryRevision:Math.max(s.profileLibraryRevision,remote.revision),
        userPresets:[...s.userPresets.filter(p=>!existing.some(e=>profileKey(e)===profileKey(p) && !equal(stored(p),stored(e)))),...existing.filter(e=>!s.userPresets.some(p=>profileKey(p)===profileKey(e) && equal(stored(p),stored(e))))]}));
      if(!plan.records.length)return {keys:reused,skipped};
      const captured=structuredClone(plan.records);keys=captured.map(profileKey);service=initial.api;
      request=freezeWrite(canonical,{operationId:`create-profiles:${randomId()}`,expectedRevision:remote.revision,document:{...remote,profiles:[...remote.profiles,...captured.map(c=>storedProfile(prepareProfile(canonical,c)))]}},captured,signature(captured));
    }
    if(getState().api!==service)throw conflict('Your profile library updated in the background. We kept your new profile draft—click Save again to finish adding it.');
    const ack=await acknowledge(request);
    if(getState().api!==service)throw conflict('Your profile library updated while saving. We kept your changes safe—try saving again.');
    publishAcknowledgement(ack);
    if(keys.some(key=>ack.changed.has(key)) || ack.records.some(p=>p.archived))throw conflict('That profile was updated right after being added. Don\'t worry, both the profile and your current setup are safe!');
    return {keys:[...reused,...keys],skipped};
  }).finally(()=>{flight=undefined;})};
}
export function profileCreation(factory:(s:State)=>UserPreset|string):ProfileCreation{
  const batch=profileBatchCreation(s=>{const created=factory(s);return typeof created==='string' ? {records:[],reused:[created]} : {records:[created]};});
  return {get retryable(){return batch.retryable;},commit:()=>batch.commit().then(result=>result.keys[0])};
}
export function profileCopyCreation(source:UserPreset,name:string):ProfileCreation{
  const captured=structuredClone(source),service=getState().api;
  return profileCreation(s=>{
    if(!captured.id || !name.trim())throw new Error('Pick a profile to copy and give your new copy a name.');
    if(!s.api || s.api!==service)throw conflict('The profile service changed before copying. Reopen the saved source; your requested copy is kept.');
    const current=findProfile(s,captured.type,captured.id);
    if(!current || !equal(stored(current),stored(captured)))throw conflict('The profile you\'re copying was just updated elsewhere. Give it a quick look before duplicating.');
    if(s.userPresets.some(p=>p.type===captured.type && p.name===name))throw new Error('You already have a profile with that name—try a different one!');
    return {...captured,id:`profile:${randomId()}`,kind:'profile',name,revision:0,aliases:[],archived:undefined,
      provenance:{source:'profile',sourceId:captured.id,sourceName:captured.name,sourceRevision:captured.revision,operationId:randomId()}};
  });
}
async function performSave(s:State,records:UserPreset[],operationId?:string,printers?:PrinterProfileLibrary,catalog?:SystemPreset[],setup?:PrinterSetupSaveContext):Promise<Acknowledgement>{
  ready(s);ready(getState());
  const requestSignature=signature(records,printers,catalog),outstanding=pending;
  const priorAck=outstanding?await acknowledge(outstanding):undefined;
  if(priorAck && outstanding!.signature===requestSignature){
    if(priorAck.changed.size || priorAck.setupsChanged){publishAcknowledgement(priorAck);throw conflict('This profile or printer setup was updated in another window right after you saved. We kept your draft so you can compare them.');}
    return priorAck;
  }
  if(priorAck)publishAcknowledgement(priorAck);
  const current=()=>{if(setup && !setup.isCurrent())throw Object.assign(new Error('Your printer setup changed in the meantime—we kept your newest setup.'),{code:'setup_context_changed'});};
  current();
  const remote=priorAck?.document ?? await readProfileLibrary(s.api?.mock);
  current();
  ready(getState());if(getState().api!==s.api)throw new Error('The profile library updated while saving. Don\'t worry, your draft edits are still here!');
  const profiles=new Map(remote.profiles.map(p=>[p.id,p]));
  if(profiles.size!==remote.profiles.length || remote.profiles.some(p=>!p.id))throw conflict('Oops! Your profile library has duplicate or missing IDs that need a quick repair.');
  const submitted:UserPreset[]=[];
  for(const p of records){
    const id=profileKey(p),prior=findProfile(s,p.type,id),raw=profiles.get(id),actual=raw && normalizeUserPreset(raw);
    if(prior && !actual)throw Object.assign(new Error('This profile was deleted elsewhere, but we kept your unsaved edits right here.'),{code:'profile_missing',recovery:'reload_profiles'});
    const own=prior && ownAcknowledgements.get(prior),ownAdvance=!!actual && !!own && equal(stored(actual),stored(own));
    if(prior && actual && (actual.revision ?? 0)>(prior.revision ?? 0) && !ownAdvance)throw conflict('Heads up: this profile was updated in another window. Your current draft edits are still kept here.');
    if(prior && (p.revision ?? 0)<(prior.revision ?? 0) && !ownAdvance)throw conflict('This profile was updated elsewhere since you started editing. Your draft edits are still kept here.');
    let prepared=prepareProfile(s,p);
    if(actual && equal({...stored(prepared),revision:0},{...stored(actual),revision:0})){submitted.push(actual);continue;}
    if(actual)prepared={...prepared,revision:Math.max(prepared.revision ?? 0,(actual.revision ?? 0)+1)};
    profiles.set(id,storedProfile(prepared));submitted.push(prepared);
  }
  if(printers && (setup ? !sameSetups(remote.printers,ownSetupAcknowledgements.get(setup.baseline) ?? setup.baseline)
    : !sameSetups(remote.printers,s.printerLibrary) && !(priorAck && sameSetups(remote.printers,priorAck.request.write.document.printers))))throw conflict('The saved printer setups changed. Reload before replacing them; your working setup is kept.');
  const catalogKeys=new Set(catalog?.map(catalogKey));
  const document={...remote,profiles:[...profiles.values()],...(catalog?.length?{catalog:[...(remote.catalog ?? []).filter(p=>!catalogKeys.has(catalogKey(p))),...catalog]}:{}),printers:printers ?? remote.printers ?? s.printerLibrary ?? emptyPrinterLibrary()};
  if(equal(document,remote))return {request:freezeWrite(s,{operationId:operationId ?? '',expectedRevision:remote.revision,document},submitted,requestSignature),document:remote,records:submitted,changed:new Set()};
  const request=freezeWrite(s,{operationId:operationId ?? `save:${randomId()}`,expectedRevision:remote.revision,document},submitted,requestSignature,printers ? setup?.baseline ?? s.printerLibrary : undefined);
  const ack=await acknowledge(request);
  if(ack.changed.size || ack.setupsChanged){publishAcknowledgement(ack);throw conflict('This profile or printer setup changed in another window. We kept your draft so you can compare them.');}
  return ack;
}
export function saveProfileRecords(s:State,records:UserPreset[],operationId?:string,printers?:PrinterProfileLibrary,catalog?:SystemPreset[],setup?:PrinterSetupSaveContext):Promise<void>{
  return serialize(async()=>{const ack=await performSave(s,records,operationId,printers,catalog,setup);publishAcknowledgement(ack);});
}
export function commitProfileRecords(records:UserPreset[],options:{operationId?:string;printers?:PrinterProfileLibrary;bases?:SystemPreset[];apply?(s:State):Partial<State>}={}):Promise<void>{
  const expected=getState();
  return serialize(async()=>{
    for(const p of records)if(!profileOriginIsCurrent(findProfile(getState(),p.type,profileKey(p)),findProfile(expected,p.type,profileKey(p))))throw conflict('This profile was updated elsewhere while waiting to save. Your draft edits are safely kept here.');
    const ack=await performSave(expected,records,options.operationId,options.printers,options.bases);
    publishAcknowledgement(ack,s=>({...options.apply?.(s),...(options.bases?Object.fromEntries((['machine','process','filament'] as const).map(type=>{const key=type==='machine'?'machines':type==='process'?'processes':'filaments',incoming=options.bases!.filter(b=>b.type===type),incomingKeys=new Set(incoming.map(catalogKey));return[key,[...s[key].filter(b=>!incomingKeys.has(catalogKey(b))),...incoming]];})):{}),...(options.printers?{printerLibrary:ack.document.printers}:{})}));
  });
}
const masterCreations=new Map<string,ProfileCreation>();
export async function ensureEditableMaster(type:PresetType,baseKey:string):Promise<string>{
  const s=getState(),existing=findProfile(s,type,baseKey);
  if(existing?.kind==='master' && profileKey(existing)===baseKey)return profileKey(existing);
  const base=findCatalogBase(s,type,baseKey).base;
  if(!base)throw new Error('Pick the exact catalog template before editing its base profile.');
  const token=catalogKey(base);
  let creation=masterCreations.get(token);
  if(!creation){
    creation=profileCreation(latest=>{
      const master=editableMaster(latest,type,token);
      return findProfile(latest,type,profileKey(master)) ? profileKey(master) : master;
    });
    masterCreations.set(token,creation);
  }
  try{
    const key=await creation.commit();
    if(masterCreations.get(token)===creation)masterCreations.delete(token);
    return key;
  }catch(error){
    if(!creation.retryable && masterCreations.get(token)===creation)masterCreations.delete(token);
    throw error;
  }
}
export interface ProfileArchiveUndo {type:PresetType;keys:string[];operationId:string}
export async function archiveProfiles(type:PresetType,keys:readonly string[]):Promise<ProfileArchiveUndo>{
  const s=getState(),operationId=`archive:${randomId()}`,records=keys.map(key=>archiveProfile(s,type,key,operationId));
  await commitProfileRecords(records,{operationId});return{type,keys:records.map(profileKey),operationId};
}
export async function restoreProfiles(undo:ProfileArchiveUndo):Promise<void>{
  const s=getState(),records=undo.keys.flatMap(key=>{const p=findProfile(s,undo.type,key);return p?.archived?.operationId===undo.operationId?[restoreArchivedProfile(p)]:[];});
  if(records.length)await commitProfileRecords(records);
}
