import type { PresetType, UserPreset } from '../api/types';
import type { State } from '../state/store';
import { effectiveProfileValues, type ProfileContextState } from './context';
import { catalogAnchor, changedKeys, findCatalogBase, resolveProfile, storedProfile } from './model';
import type { ProjectProfileReferences, ProjectProfileSnapshot, ProfileRecovery } from './ownership';
const cache=new WeakMap<ProfileContextState,ProjectProfileReferences>();
const shared=new WeakMap<UserPreset[],{inputs:unknown[];value:ProjectProfileReferences}>();
export function captureProjectProfiles(s:ProfileContextState & Partial<Pick<State,'printerLibrary'|'slotColours'>>):ProjectProfileReferences{
  const ready=cache.get(s);if(ready)return ready;
  const keys:[PresetType,string,number][]=[['machine',s.printer,0],['process',s.process,0],...(s.filamentSlots ?? []).map((key,slot)=>['filament',key,slot] as [PresetType,string,number])];
  const configs=keys.map(([type,key,slot])=>effectiveProfileValues(s,type,key,slot));
  const slots=s.profileSnapshots?.printer.id===s.printer && s.profileSnapshots.slots?.length===s.filamentSlots?.length?s.profileSnapshots.slots:s.printerLibrary?.setups[s.printer]?.slots;
  const inputs=[s.printer,s.process,s.filamentSlots,s.slotColours,slots,...configs],prior=shared.get(s.userPresets);
  if(prior && inputs.length===prior.inputs.length && inputs.every((v,i)=>v===prior.inputs[i])){cache.set(s,prior.value);return prior.value;}
  const records=keys.map(([type,key],index):ProjectProfileSnapshot=>{
    const resolved=resolveProfile(s,type,key),source=resolved.source;
    const captured=type==='machine'?s.profileSnapshots?.printer:type==='process'?s.profileSnapshots?.process:s.profileSnapshots?.filaments[index-2];
    if(captured?.id===key)return{...captured,config:configs[index]};
    return {id:resolved.key ?? key,name:resolved.name ?? key,type,revision:source?.revision ?? 0,printerId:source?.printerId,
      base:source?.base ?? (resolved.base ? catalogAnchor(resolved.base) : {name:key,version:''}),config:configs[index],profileConfig:resolved.config,
      ...(source?{profile:storedProfile(source)}:{})};
  });
  const value:ProjectProfileReferences={schema:1,printer:records[0],process:records[1],filaments:records.slice(2),
    ...(slots?{slots:slots.map((slot,i)=>({...slot,colour:s.slotColours?.[i] ?? slot.colour}))}:{})};
  cache.set(s,value);shared.set(s.userPresets,{inputs,value});return value;
}
export { parseProjectProfiles } from './project-snapshot-codec';
export function restoreProjectProfiles(s:ProfileContextState,snapshots:ProjectProfileReferences){
  const recoveries:ProfileRecovery[]=[];
  for(const row of [snapshots.printer,snapshots.process,...snapshots.filaments]){
    const resolved=resolveProfile(s,row.type,row.id);
    if(resolved.issue)recoveries.push({type:row.type,key:row.id,reason:resolved.issue.code,snapshot:{base:row.base,config:row.config}});
  }
  return {printer:snapshots.printer.id,process:snapshots.process.id,filamentSlots:snapshots.filaments.map(row=>row.id),profileSnapshots:snapshots,profileRecoveries:recoveries,
    ...(snapshots.slots?{slotColours:snapshots.slots.map(slot=>slot.colour)}:{})};
}
export function recoverProjectProfile(s:ProfileContextState,snapshot:ProjectProfileSnapshot,baseKey:string,name=snapshot.name):UserPreset{
  const base=findCatalogBase(s,snapshot.type,baseKey).base;if(!base)throw new Error('Choose an available base before recovering this profile.');
  const config=snapshot.profileConfig ?? snapshot.profile?.snapshot?.config ?? snapshot.config,overlay=changedKeys(config,base.config);
  return {...snapshot.profile,id:`profile:${crypto.randomUUID()}`,name,type:snapshot.type,kind:snapshot.profile?.kind ?? 'profile',revision:0,archived:undefined,
    base:catalogAnchor(base),inherits:base.name,parent:undefined,parentId:undefined,masterId:undefined,overlay,overrides:overlay,
    snapshot:{base:catalogAnchor(base),config},provenance:{source:'import',sourceId:snapshot.id,sourceName:snapshot.name,operationId:`recover:${crypto.randomUUID()}`}};
}
export const projectProfileIds=(s:Pick<State,'printer'|'process'|'filamentSlots'> & Partial<ProfileContextState>)=>{
  const name=(type:PresetType,key:string,slot=0)=>{
    const captured=type==='machine'?s.profileSnapshots?.printer:type==='process'?s.profileSnapshots?.process:s.profileSnapshots?.filaments[slot];
    return captured?.id===key?captured.name:s.userPresets && s.machines && s.processes && s.filaments ? resolveProfile(s as ProfileContextState,type,key).name ?? key : key;
  };
  return {printer_settings_id:name('machine',s.printer),print_settings_id:name('process',s.process),filament_settings_id:s.filamentSlots.map((key,slot)=>name('filament',key,slot))};
};
export async function stampProjectProfiles(blob:Blob,s:Pick<State,'printer'|'process'|'filamentSlots'> & Partial<ProfileContextState>):Promise<Blob>{
  const {strFromU8,strToU8,unzipSync,zipSync}=await import('fflate');
  const files=unzipSync(new Uint8Array(await blob.arrayBuffer())),path=Object.keys(files).find(p=>p.toLowerCase()==='metadata/project_settings.config');
  if(!path)return blob;
  const config=JSON.parse(strFromU8(files[path]));
  const snapshots=s.userPresets && s.machines && s.processes && s.filaments ? {orca_web_profiles:JSON.stringify(captureProjectProfiles(s as ProfileContextState))} : {};
  files[path]=strToU8(JSON.stringify({...config,...projectProfileIds(s),...snapshots}));
  return new Blob([zipSync(files) as BlobPart],{type:blob.type});
}
