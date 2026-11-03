import type { Config, PresetType } from '../api/types';
import { getState, resolvePreset, toast } from '../state/store';
import { findProfile } from './identity';
import { selectCompatibility } from './compatibility';
import * as catalog from './catalog';
import { requestPreset } from '../components/PresetCombo';
import { documentGeneration } from '../state/history';

export interface PickerEntry {
  key: string;
  name: string;
  config: Config;
  vendor: string;
  mine: boolean;
  reason?: string;
}

export function listProfiles(type: PresetType): PickerEntry[] {
  const s = getState();
  return catalog.listProfiles(s,type).map(row => {
    const r = resolvePreset(s,type,row.key);
    return {key:row.key,name:row.name,config:r.config,vendor:r.base?.vendor ?? '',mine:true};
  });
}

export function listCatalog(type: PresetType): PickerEntry[] {
  const s = getState();
  return catalog.listCatalog(s,type).filter(row => row.compatible.ok).map(row => ({key:row.name,name:row.name,
    config:resolvePreset(s,type,row.name).config,vendor:row.vendor,mine:false}));
}

const callbacks=new WeakMap<(key:string)=>void,number>();let nextCallback=0;
type Intent={owner:ReturnType<typeof selectionOwner>;flight:Promise<void>};
const intents=new Map<string,Intent>();
function selectionOwner(){
  const s=getState();
  return {document:documentGeneration(),api:s.api,printer:s.printer,process:s.process,slots:s.filamentSlots,colours:s.slotColours,
    setup:s.printerLibrary.setups[s.printer],machineEdits:s.dirty.machine,processEdits:s.dirty.process,filamentEdits:s.filamentEdits,
    project:s.project,projectPresets:s.projectPresets,snapshots:s.profileSnapshots};
}
const sameOwner=(a:ReturnType<typeof selectionOwner>,b:ReturnType<typeof selectionOwner>)=>Object.keys(a).every(k=>a[k as keyof typeof a]===b[k as keyof typeof b]);
export function createProfileFromBase(type:PresetType,base:string,slot=0,select?:(key:string)=>void):Promise<void>{
  let callback=0;
  if(select){callback=callbacks.get(select) ?? ++nextCallback;callbacks.set(select,callback);}
  const key=JSON.stringify([type,base,slot,callback]),owner=selectionOwner(),pending=intents.get(key);
  if(pending && sameOwner(pending.owner,owner))return pending.flight;
  const intent:Intent={owner,flight:Promise.resolve()};intents.set(key,intent);
  intent.flight=(async()=>{
    const name=await catalog.createProfileFromBase(type,base);
    if(intents.get(key)!==intent || !sameOwner(owner,selectionOwner()))return;
    intents.delete(key);
    const s=getState(),profile=findProfile(s,type,name);
    if(!profile || profile.archived || profile.kind==='master' || !selectCompatibility(s,type,name).ok)throw new Error('The added profile needs compatibility repair before use. It remains in your library.');
    (select ?? ((value)=>requestPreset(type,slot,value)))(name);
  })().catch(error=>{toast(`Add profile failed: ${error instanceof Error?error.message:String(error)}`);}).finally(()=>{if(intents.get(key)===intent)intents.delete(key);});
  return intent.flight;
}
