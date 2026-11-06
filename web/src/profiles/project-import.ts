import { splitFilamentConfig } from '../multinozzle/config';

import type { Config, PresetType } from '../api/types';
import type { State } from '../state/store';
import { normalizeConfig } from '../config-values';
import { TABS } from '../options';
import { lookupProfile, profileKey } from './identity';
import { catalogAnchor, findCatalogBase, PRESET_META, profilesFor, resolveProfile, storedProfile } from './model';
import { parseProjectProfiles } from './project-snapshot-codec';
import { restoreProjectProfiles } from './project';
import { selectCompatibility } from './compatibility';
import type { ProjectProfileReferences, ProjectProfileSnapshot, ProfileRecovery } from './ownership';


export function prepareProjectProfileImport(s:State,config:Config,slots:number,plateKeys:ReadonlySet<string>):Partial<State>{
  const saved=normalizeConfig(config),extension=parseProjectProfiles(saved.orca_web_profiles);
  const printerName=String(saved.printer_settings_id ?? '');
  if(!extension && !printerName && saved.printer_model===undefined)return{projectPresets:null,profileSnapshots:null,projectProfileContexts:{},profileRecoveries:[],projectNotice:null,mixes:{}};
  const scopeKeys=(type:PresetType)=>{
    const keys=new Set(profilesFor(s,type).flatMap(p=>Object.keys(p.config)));
    for(const p of s.userPresets)if(p.type===type)for(const key of Object.keys(p.overlay ?? p.overrides))keys.add(key);
    const tab=TABS[type==='machine'?'printer':type];
    for(const page of tab?.pages ?? [])for(const group of page.groups)for(const key of group.options)keys.add(key.split('#')[0]);
    return keys;
  };
  const machineKeys=scopeKeys('machine'),filamentKeys=scopeKeys('filament');

  for(const key of ['printer_model','printer_variant','nozzle_diameter','nonplanar_kinematics','nonplanar_rotary_axes'])machineKeys.add(key);
  const take=(scope:PresetType,index=0):Config=>Object.fromEntries(Object.entries(scope==='filament'?splitFilamentConfig(saved,index):saved).filter(([key])=>
    key!=='orca_web_profiles' && !PRESET_META.test(key) && !plateKeys.has(key) && (scope==='machine'?machineKeys.has(key):scope==='filament'?filamentKeys.has(key)&&!machineKeys.has(key):!machineKeys.has(key)&&!filamentKeys.has(key)))
    ) as Config;
  const recoveries:ProfileRecovery[]=[];
  const capture=(type:PresetType,reference:string,values:Config):ProjectProfileSnapshot=>{
    const lookup=lookupProfile(s,type,reference),source=lookup.profile;
    const found=source?undefined:findCatalogBase(s,type,reference),resolved=source?resolveProfile(s,type,profileKey(source)):undefined;

    const id=source?profileKey(source):`project-profile:${crypto.randomUUID()}`;
    const base=source?.base ?? (found?.base?catalogAnchor(found.base):{name:reference,version:''});
    const row:ProjectProfileSnapshot={id,name:(source && reference===profileKey(source)?source.name:reference) || (type==='machine'?'Project printer':type==='filament'?'Project filament':'Project print settings'),type,
      revision:source?.revision ?? 0,printerId:source?.printerId,base,config:{...(resolved?.config ?? found?.base?.config),...values},profileConfig:resolved?.config ?? found?.base?.config ?? values,
      ...(source?{profile:storedProfile(source)}:{})};
    const reason=lookup.ambiguous || found?.ambiguous?'ambiguous':resolved?.issue?.code ?? (!source?'missing-profile':undefined);
    if(reason)recoveries.push({type,key:id,reason,snapshot:{base,config:row.config}});
    return row;
  };
  let snapshots:ProjectProfileReferences;
  if(extension)snapshots=extension;
  else{
    const names=saved.filament_settings_id,count=Math.max(1,Number.isFinite(slots)?Math.trunc(slots):0,Array.isArray(names)?names.length:0);
    snapshots={schema:1,printer:capture('machine',printerName,take('machine')),process:capture('process',String(saved.print_settings_id ?? ''),take('process')),
      filaments:Array.from({length:count},(_,i)=>capture('filament',String(Array.isArray(names)?names[i] ?? '':names ?? ''),take('filament',i)))};
    const colours=saved.filament_colour;
    snapshots.slots=snapshots.filaments.map((row,i)=>({id:`project-slot:${crypto.randomUUID()}`,materialId:row.id,
      colour:Array.isArray(colours)?String(colours[i] ?? '') || undefined:typeof colours==='string'?colours:undefined,snapshot:row}));
  }
  const restored=restoreProjectProfiles(s,snapshots),projectPresets={machine:snapshots.printer.config,process:snapshots.process.config,filaments:snapshots.filaments.map(row=>row.config)};
  const dirty={machine:{},process:{}},filamentEdits={};
  const next={...s,...restored,projectPresets,dirty,filamentEdits};
  const all=[...recoveries,...restored.profileRecoveries];
  for(const row of [snapshots.process,...snapshots.filaments]){
    if(all.some(r=>r.key===row.id))continue;
    if(!selectCompatibility(next,row.type,row.id).ok)all.push({type:row.type,key:row.id,reason:'incompatible',snapshot:{base:row.base,config:row.config}});
  }
  const unique=all.filter((r,i)=>all.findIndex(other=>other.type===r.type && other.key===r.key)===i);
  return{...restored,profileRecoveries:unique,projectPresets,dirty,filamentEdits,projectProfileContexts:{},
    projectNotice:unique.length?'Project settings were retained. Save or repair the indicated profile references before slicing.':null,
    bedType:String(saved.curr_bed_type ?? s.bedType),mixes:{}};
}
