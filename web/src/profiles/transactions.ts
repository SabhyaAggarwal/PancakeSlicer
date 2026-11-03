import type { PresetType, SystemPreset, UserPreset } from '../api/types';
import { getState, markStale, setState, type State } from '../state/store';
import { readProfileLibrary, persistProfileLibrary } from '../persist/profile-library';
import { emptyPrinterLibrary, type PrinterProfileLibrary } from './ownership';
import { findProfile, profileKey } from './identity';
import { normalizeUserPreset, prepareProfile, storedProfile } from './model';
import { editableMaster, archiveProfile, restoreArchivedProfile } from './library-actions';
let tail:Promise<unknown>=Promise.resolve();
const serialize = <T>(action:()=>Promise<T>):Promise<T> => {const result=tail.then(action);tail=result.catch(()=>{});return result;};
export async function saveProfileRecords(s: State, records: UserPreset[], operationId = `save:${crypto.randomUUID()}`, printers?:PrinterProfileLibrary,catalog?:SystemPreset[]):Promise<void> {
  if(s.profileLibraryLoad.state!=='ready')throw new Error(s.profileLibraryLoad.state==='failed'?s.profileLibraryLoad.error:'Wait for the saved profile library to load. Your draft is kept.');
  const remote=await readProfileLibrary(s.api?.mock),mock=s.api?.mock ?? false;

  const profiles=new Map((remote?.profiles ?? []).map(p=>[p.id,normalizeUserPreset(p)]));
  for(const p of records){
    const prior=findProfile(s,p.type,profileKey(p)),stored=profiles.get(profileKey(p));
    if(remote?.revision && prior && !stored)throw Object.assign(new Error('This profile was removed from storage. Reload before saving this draft.'),{code:'profile_missing',recovery:'reload_profiles'});
    if(prior && stored && (stored.revision ?? 0)>(prior.revision ?? 0))throw Object.assign(new Error('This profile changed in another window. Reload before saving this draft.'),{code:'revision_conflict',recovery:'reload_profiles'});
    profiles.set(profileKey(p),p);
  }
  const revision=remote?.revision ?? 0;
  if(printers && s.profileLibraryRevision>0 && revision!==s.profileLibraryRevision)throw Object.assign(new Error('The saved printer setups changed. Reload before replacing them; your working setup is kept.'),{code:'revision_conflict',recovery:'reload_profiles'});
  const committed=await persistProfileLibrary({operationId,expectedRevision:revision,document:{schema:1,revision,
    profiles:[...profiles.values()].map(p=>storedProfile(prepareProfile(s,p))),...(catalog?.length ? {catalog:[...(remote?.catalog ?? []).filter(p=>!catalog.some(n=>n.type===p.type && n.vendor===p.vendor && n.name===p.name)),...catalog]} : remote?.catalog ? {catalog:remote.catalog} : {}),printers:printers ?? remote?.printers ?? s.printerLibrary ?? emptyPrinterLibrary()}},mock);
  setState({profileLibraryRevision:committed.revision});
}
export function commitProfileRecords(records:UserPreset[], options:{operationId?:string;printers?:PrinterProfileLibrary;bases?:SystemPreset[];apply?(s:State):Partial<State>}={}):Promise<void>{
  const expected=getState();
  return serialize(async()=>{
    for(const p of records){const before=findProfile(expected,p.type,profileKey(p)),now=findProfile(getState(),p.type,profileKey(p));if(before!==now)throw new Error('This profile changed while waiting to save. Reload this draft.');}
    const current=getState();await saveProfileRecords(current,records,options.operationId,options.printers,options.bases);
    const ids=new Set(records.map(profileKey));
    setState(s=>({...options.apply?.(s),job:markStale(s.job),...(options.bases ? Object.fromEntries((['machine','process','filament'] as const).map(type=>{const key=type==='machine'?'machines':type==='process'?'processes':'filaments';const incoming=options.bases!.filter(b=>b.type===type);return[key,[...s[key].filter(b=>!incoming.some(n=>n.name===b.name&&n.vendor===b.vendor)),...incoming]];})) : {}),userPresets:[...s.userPresets.filter(p=>!ids.has(profileKey(p))),...records],...(options.printers?{printerLibrary:options.printers}:{})}));
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
