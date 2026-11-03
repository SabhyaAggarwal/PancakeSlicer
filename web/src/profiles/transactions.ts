import type { PresetType, SystemPreset, UserPreset } from '../api/types';
import { getState, markStale, setState, type State } from '../state/store';
import { readProfileLibrary, persistProfileLibrary } from '../persist/profile-library';
import { emptyPrinterLibrary, type PrinterProfileLibrary, type ProfileLibraryWrite } from './ownership';
import { findProfile, profileKey } from './identity';
import { normalizeUserPreset, prepareProfile, storedProfile } from './model';
import { editableMaster, archiveProfile, restoreArchivedProfile } from './library-actions';
let tail:Promise<unknown>=Promise.resolve();
const serialize = <T>(action:()=>Promise<T>):Promise<T> => {const result=tail.then(action);tail=result.catch(()=>{});return result;};
const ordered=(value:unknown):unknown=>Array.isArray(value)?value.map(ordered):value && typeof value==='object'?Object.fromEntries(Object.entries(value).sort(([a],[b])=>a.localeCompare(b)).filter(([,v])=>v!==undefined).map(([k,v])=>[k,ordered(v)])):value;
const equal=(a:unknown,b:unknown)=>JSON.stringify(ordered(a))===JSON.stringify(ordered(b));
const stored=(p:UserPreset)=>storedProfile(normalizeUserPreset(p));
const conflict=(message:string)=>Object.assign(new Error(message),{code:'revision_conflict',recovery:'reload_profiles'});
interface FrozenWrite {write:ProfileLibraryWrite;records:UserPreset[];origins:Map<string,UserPreset|undefined>;signature:string;retryable:boolean}
interface Acknowledgement {request:FrozenWrite;document:import('./ownership').ProfileLibraryDocument;records:UserPreset[];changed:Set<string>}
let pending:FrozenWrite|undefined;
const ownAcknowledgements=new WeakMap<UserPreset,UserPreset>();
export function profileOriginIsCurrent(current:UserPreset|undefined,origin:UserPreset|undefined):boolean{
  return current===origin || !!current && !!origin && !!ownAcknowledgements.get(origin) && equal(stored(current),stored(ownAcknowledgements.get(origin)!));
}
const signature=(records:UserPreset[],printers?:PrinterProfileLibrary,catalog?:SystemPreset[])=>JSON.stringify(ordered({records:records.map(stored),printers,catalog}));
const ready=(s:State)=>{if(s.profileLibraryLoad.state!=='ready')throw new Error(s.profileLibraryLoad.state==='failed'?s.profileLibraryLoad.error:'Wait for the saved profile library to load. Your draft is kept.');};
function freezeWrite(s:State,write:ProfileLibraryWrite,records:UserPreset[],requestSignature:string):FrozenWrite{
  return {write:structuredClone(write),records,signature:requestSignature,retryable:true,origins:new Map(records.map(p=>[profileKey(p),findProfile(s,p.type,profileKey(p))]))};
}
async function acknowledge(request:FrozenWrite):Promise<Acknowledgement>{
  pending=request;
  try{await persistProfileLibrary(request.write);}
  catch(error){
    const failure=error as {status?:number;code?:string};
    if(failure.code==='revision_conflict' || [400,401,403,404,422].includes(failure.status ?? 0)){request.retryable=false;if(pending===request)pending=undefined;}
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
  request.retryable=false;if(pending===request)pending=undefined;
  return {request,document,records,changed};
}
function publishAcknowledgement(ack:Acknowledgement,apply?:(s:State)=>Partial<State>){
  const ids=new Set(ack.records.map(profileKey));
  setState(s=>({...apply?.(s),profileLibraryRevision:Math.max(s.profileLibraryRevision,ack.document.revision),job:markStale(s.job),
    userPresets:!ids.size?s.userPresets:[...s.userPresets.filter(p=>!ids.has(profileKey(p))),...ack.records.map(p=>{const current=findProfile(s,p.type,profileKey(p));return current && ((current.revision ?? 0)>(p.revision ?? 0) || equal(stored(current),stored(p)))?current:p;})]}));
}
async function settleOutstanding():Promise<Acknowledgement|undefined>{
  if(!pending)return;
  const ack=await acknowledge(pending);publishAcknowledgement(ack);return ack;
}
export interface ProfileCreation {commit():Promise<string>;readonly retryable:boolean}
export function profileCreation(factory:(s:State)=>UserPreset|string):ProfileCreation{
  let request:FrozenWrite|undefined,key:string|undefined;
  return {get retryable(){return !!request?.retryable;},commit:()=>serialize(async()=>{
    ready(getState());
    if(pending && pending!==request)await settleOutstanding();
    if(!request || !request.retryable){
      const initial=getState();ready(initial);
      const remote=await readProfileLibrary(initial.api?.mock),latest=getState();ready(latest);
      if(latest.api!==initial.api)throw new Error('The profile service changed while adding. Your current setup is kept.');
      const records=remote.profiles.map(normalizeUserPreset);
      if(records.some(p=>!p.id) || new Set(records.map(profileKey)).size!==records.length)throw new Error('The saved identities need recovery before adding.');
      const canonical={...latest,userPresets:records},created=factory(canonical);
      if(typeof created==='string'){
        const record=records.find(p=>profileKey(p)===created);if(!record)throw new Error('The reusable profile is unavailable.');
        setState(s=>{const current=findProfile(s,record.type,created);return {profileLibraryRevision:Math.max(s.profileLibraryRevision,remote.revision),
          userPresets:current && equal(stored(current),stored(record))?s.userPresets:[...s.userPresets.filter(p=>profileKey(p)!==created),record]};});return created;
      }
      key=profileKey(created);
      request=freezeWrite(canonical,{operationId:`create-profile:${crypto.randomUUID()}`,expectedRevision:remote.revision,document:{...remote,profiles:[...remote.profiles,storedProfile(prepareProfile(canonical,created))]}},[created],signature([created]));
    }
    const ack=await acknowledge(request);publishAcknowledgement(ack);
    if(ack.changed.has(key!) || ack.records.some(p=>p.archived))throw conflict('The added profile changed before selection. Its current record and your setup are kept.');
    return key!;
  })};
}
async function performSave(s:State,records:UserPreset[],operationId?:string,printers?:PrinterProfileLibrary,catalog?:SystemPreset[]):Promise<Acknowledgement>{
  ready(s);ready(getState());
  const requestSignature=signature(records,printers,catalog),outstanding=pending;
  const priorAck=outstanding?await acknowledge(outstanding):undefined;
  if(priorAck && outstanding!.signature===requestSignature){
    if(priorAck.changed.size){publishAcknowledgement(priorAck);throw conflict('The saved profile changed after your write. Your draft is kept for comparison.');}
    return priorAck;
  }
  if(priorAck)publishAcknowledgement(priorAck);
  const remote=priorAck?.document ?? await readProfileLibrary(s.api?.mock);
  ready(getState());if(getState().api!==s.api)throw new Error('The profile service changed while saving. Your draft is kept.');
  const profiles=new Map(remote.profiles.map(p=>[p.id,p]));
  if(profiles.size!==remote.profiles.length || remote.profiles.some(p=>!p.id))throw conflict('The saved library contains duplicate or missing identities.');
  const submitted:UserPreset[]=[];
  for(const p of records){
    const id=profileKey(p),prior=findProfile(s,p.type,id),raw=profiles.get(id),actual=raw && normalizeUserPreset(raw);
    if(prior && !actual)throw Object.assign(new Error('This profile was removed from storage. Your draft is kept.'),{code:'profile_missing',recovery:'reload_profiles'});
    const own=prior && ownAcknowledgements.get(prior),ownAdvance=!!actual && !!own && equal(stored(actual),stored(own));
    if(prior && actual && (actual.revision ?? 0)>(prior.revision ?? 0) && !ownAdvance)throw conflict('This profile changed in another window. Your draft is kept.');
    if(prior && (p.revision ?? 0)<(prior.revision ?? 0) && !ownAdvance)throw conflict('This save belongs to an older profile revision. Your draft is kept.');
    let prepared=prepareProfile(s,p);
    if(actual && equal({...stored(prepared),revision:0},{...stored(actual),revision:0})){submitted.push(actual);continue;}
    if(actual)prepared={...prepared,revision:Math.max(prepared.revision ?? 0,(actual.revision ?? 0)+1)};
    profiles.set(id,storedProfile(prepared));submitted.push(prepared);
  }
  if(printers && s.profileLibraryRevision>0 && remote.revision!==s.profileLibraryRevision && !(priorAck && equal(remote.printers,priorAck.request.write.document.printers)))throw conflict('The saved printer setups changed. Reload before replacing them; your working setup is kept.');
  const document={...remote,profiles:[...profiles.values()],...(catalog?.length?{catalog:[...(remote.catalog ?? []).filter(p=>!catalog.some(n=>n.type===p.type && n.vendor===p.vendor && n.name===p.name)),...catalog]}:{}),printers:printers ?? remote.printers ?? s.printerLibrary ?? emptyPrinterLibrary()};
  if(equal(document,remote))return {request:freezeWrite(s,{operationId:operationId ?? '',expectedRevision:remote.revision,document},submitted,requestSignature),document:remote,records:submitted,changed:new Set()};
  const request=freezeWrite(s,{operationId:operationId ?? `save:${crypto.randomUUID()}`,expectedRevision:remote.revision,document},submitted,requestSignature);
  const ack=await acknowledge(request);
  if(ack.changed.size){publishAcknowledgement(ack);throw conflict('This profile changed after your write. Your draft is kept for comparison.');}
  return ack;
}
export function saveProfileRecords(s:State,records:UserPreset[],operationId?:string,printers?:PrinterProfileLibrary,catalog?:SystemPreset[]):Promise<void>{
  return serialize(async()=>{const ack=await performSave(s,records,operationId,printers,catalog);publishAcknowledgement(ack);});
}
export function commitProfileRecords(records:UserPreset[],options:{operationId?:string;printers?:PrinterProfileLibrary;bases?:SystemPreset[];apply?(s:State):Partial<State>}={}):Promise<void>{
  const expected=getState();
  return serialize(async()=>{
    for(const p of records)if(!profileOriginIsCurrent(findProfile(getState(),p.type,profileKey(p)),findProfile(expected,p.type,profileKey(p))))throw conflict('This profile changed while waiting to save. Your draft is kept.');
    const ack=await performSave(expected,records,options.operationId,options.printers,options.bases);
    publishAcknowledgement(ack,s=>({...options.apply?.(s),...(options.bases?Object.fromEntries((['machine','process','filament'] as const).map(type=>{const key=type==='machine'?'machines':type==='process'?'processes':'filaments',incoming=options.bases!.filter(b=>b.type===type);return[key,[...s[key].filter(b=>!incoming.some(n=>n.name===b.name&&n.vendor===b.vendor)),...incoming]];})):{}),...(options.printers?{printerLibrary:ack.document.printers}:{})}));
  });
}
export async function ensureEditableMaster(type:PresetType,baseKey:string):Promise<string>{
  const s=getState(),master=editableMaster(s,type,baseKey);
  if(!findProfile(s,type,profileKey(master)))await commitProfileRecords([master]);
  return profileKey(master);
}
export interface ProfileArchiveUndo {type:PresetType;keys:string[];operationId:string}
export async function archiveProfiles(type:PresetType,keys:readonly string[]):Promise<ProfileArchiveUndo>{
  const s=getState(),operationId=`archive:${crypto.randomUUID()}`,records=keys.map(key=>archiveProfile(s,type,key,operationId));
  await commitProfileRecords(records,{operationId});return{type,keys:records.map(profileKey),operationId};
}
export async function restoreProfiles(undo:ProfileArchiveUndo):Promise<void>{
  const s=getState(),records=undo.keys.flatMap(key=>{const p=findProfile(s,undo.type,key);return p?.archived?.operationId===undo.operationId?[restoreArchivedProfile(p)]:[];});
  if(records.length)await commitProfileRecords(records);
}
