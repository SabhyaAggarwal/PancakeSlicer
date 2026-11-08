import type { Config, PresetType } from '../api/types';
import { documentGeneration } from '../state/history';
import { selectChanges } from '../state/changes';
import { sameValue } from '../state/value-equality';
import { getState, presetEdits, resolvePreset, setState, type State } from '../state/store';
import { profileKey } from './identity';
export interface ProjectProfileChanges {
  id:string;type:PresetType;key:string;slot:number;generation:number;
  deltas:Config;edits:Config;project:Config|null;
}
export function captureProjectProfileChanges(type:PresetType,slot=0):ProjectProfileChanges {
  const s=getState(),key=(type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
  const resolved=resolvePreset(s,type,key);
  if(!resolved.source || resolved.error)throw new Error(resolved.error ?? 'Select an owned profile before saving project changes.');
  const deltas=Object.fromEntries(selectChanges(s,type,slot).filter(c=>c.scope === 'project' && c.owner.plate === undefined).map(c=>[c.key,c.now]));
  return {id:crypto.randomUUID(),type,key:profileKey(resolved.source),slot,generation:documentGeneration(),deltas:structuredClone(deltas),edits:structuredClone(presetEdits(s,type,slot)),project:structuredClone((type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type]) ?? null)};
}
export function reviewProjectProfileChanges(type:PresetType,slot=0) {
  const stage=captureProjectProfileChanges(type,slot);
  setState({presetUx:{kind:'presets',type,name:stage.key,edit:true,stage}});
}
export function acknowledgeProjectProfileChanges(s:State,captured:ProjectProfileChanges,saved:Config):Partial<State> {
  const {type,slot}=captured,key=type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  if(documentGeneration() !== captured.generation || key !== captured.key)return {};
  const edits=presetEdits(s,type,slot),project=(type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type]) ?? {};
  const nextEdits={...edits},nextProject={...project};
  for(const [key,value] of Object.entries(captured.deltas)) {
    if(!sameValue(key,saved[key],value))continue;
    if(sameValue(key,edits[key],captured.edits[key]))delete nextEdits[key];
    if(sameValue(key,project[key],captured.project?.[key]))delete nextProject[key];
  }
  return {...(type === 'filament' ? {filamentEdits:{...s.filamentEdits,[captured.key]:nextEdits}} : {dirty:{...s.dirty,[type]:nextEdits}}),...(s.projectPresets ? {projectPresets:type === 'filament' ? {...s.projectPresets,filaments:s.projectPresets.filaments.map((p,i)=>i === slot ? nextProject : p)} : {...s.projectPresets,[type]:nextProject}} : {})};
}
