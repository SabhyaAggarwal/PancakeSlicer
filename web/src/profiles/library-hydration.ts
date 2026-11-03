import type { PresetType } from '../api/types';
import type { State } from '../state/store';
import { findProfile, profileKey } from './identity';
import { normalizeUserPreset, resolveProfile } from './model';
import { selectCompatibility } from './compatibility';
import type { ProfileLibraryDocument, ProfileRecovery } from './ownership';

export interface ProfileLibraryHydration {
  patch: Partial<State>;
  requiresAdoption: boolean;
}
export function prepareProfileLibraryHydration(s: State, document: ProfileLibraryDocument, options:{restoreStartupPlaceholder?:boolean}={}):ProfileLibraryHydration {
  const userPresets=document.profiles.map(normalizeUserPreset),ids=userPresets.map(profileKey);
  if(new Set(ids).size!==ids.length)throw new Error('The saved library contains duplicate profile identities. Export it before recovery.');
  let next:State={...s,userPresets,printerLibrary:document.printers,profileLibraryRevision:document.revision};
  const patch:Partial<State>={userPresets,printerLibrary:document.printers,profileLibraryRevision:document.revision};
  for(const type of ['machine','process','filament'] as const){
    const key=type==='machine'?'machines':type==='process'?'processes':'filaments',incoming=document.catalog?.filter(p=>p.type===type) ?? [];
    if(incoming.length){patch[key]=[...s[key].filter(p=>!incoming.some(b=>b.name===p.name && b.vendor===p.vendor)),...incoming];next={...next,[key]:patch[key]};}
  }
  const differs=(id:string)=>JSON.stringify(s.printerLibrary.setups[id])!==JSON.stringify(document.printers.setups[id]);
  const authoredSetups=Object.keys(s.printerLibrary.setups).filter(differs);
  if(authoredSetups.some(id=>id!==s.printer || !options.restoreStartupPlaceholder)){
    return {patch:Object.fromEntries(Object.entries(patch).filter(([key])=>['machines','processes','filaments'].includes(key))),requiresAdoption:true};
  }
  const owned=(type:PresetType)=>userPresets.filter(p=>p.type===type && !p.archived && p.kind!=='master').sort((a,b)=>profileKey(a).localeCompare(profileKey(b)));
  const recoveries:ProfileRecovery[]=[];
  const selected=(type:PresetType,key:string):string=>{
    const known=findProfile(next,type,key);
    if(known)return profileKey(known);
    const wrappers=owned(type).filter(p=>p.base?.name===key && !p.parent && !p.parentId && !Object.keys(p.overlay ?? p.overrides).length);
    if(wrappers.length===1)return profileKey(wrappers[0]);
    return key;
  };
  next.printer=s.profileSnapshots ? s.printer : selected('machine',s.printer);
  if(!next.printer || options.restoreStartupPlaceholder && !s.profileSnapshots && !findProfile(next,'machine',next.printer))next.printer=owned('machine')[0]?.id ?? '';
  const setup=document.printers.setups[next.printer];
  next.process=s.profileSnapshots ? s.process : selected('process',s.process);
  next.filamentSlots=s.profileSnapshots ? s.filamentSlots : s.filamentSlots.map(key=>selected('filament',key));
  if(setup && !s.profileSnapshots){
    next={...next,process:setup.processId ?? next.process,filamentSlots:setup.slots.map(slot=>slot.materialId),slotColours:setup.slots.map(slot=>slot.colour),filamentEdits:structuredClone(setup.tuning),mixes:structuredClone(setup.mixes)};
  }else if(!s.profileSnapshots && (!s.printer || options.restoreStartupPlaceholder)){
    if(!findProfile(next,'process',next.process))next.process=owned('process').find(p=>selectCompatibility(next,'process',profileKey(p)).ok)?.id ?? '';
    if(!next.filamentSlots.length)next.filamentSlots=owned('filament').filter(p=>selectCompatibility(next,'filament',profileKey(p)).ok).slice(0,1).map(profileKey);
  }
  for(const [type,key] of [['machine',next.printer],['process',next.process],...next.filamentSlots.map(key=>['filament',key])] as [PresetType,string][]){
    const resolved=resolveProfile(next,type,key);
    if(resolved.issue)recoveries.push({type,key,reason:resolved.issue.code,snapshot:resolved.source?.snapshot});
    else if(type!=='machine' && !selectCompatibility(next,type,key).ok)recoveries.push({type,key,reason:'incompatible',snapshot:resolved.source?.snapshot});
  }
  return {patch:{...patch,printer:next.printer,process:next.process,filamentSlots:next.filamentSlots,slotColours:next.slotColours,filamentEdits:next.filamentEdits,mixes:next.mixes,profileRecoveries:recoveries},requiresAdoption:false};
}
