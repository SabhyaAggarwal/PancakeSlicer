import type { Config, PresetType } from '../api/types';
import type { ProjectProfileReferences, ProjectProfileSnapshot } from './ownership';
const configObject=(value:unknown):value is Config=>!!value && typeof value==='object' && !Array.isArray(value) && Object.values(value).every(v=>['string','number','boolean'].includes(typeof v)||Array.isArray(v)&&v.every(x=>typeof x==='string'||typeof x==='number'));
export function parseProjectProfiles(value:unknown):ProjectProfileReferences|undefined{
  if(value===undefined)return undefined;
  let parsed:unknown;try{parsed=typeof value==='string'?JSON.parse(value):value;}catch{throw new Error('The saved profile snapshot is invalid. Keep the original project file for recovery.');}
  const p=parsed as ProjectProfileReferences;
  const valid=(r:ProjectProfileSnapshot,type:PresetType)=>r && r.type===type && typeof r.id==='string' && typeof r.name==='string' && r.base && typeof r.base.name==='string' && typeof r.base.version==='string' && configObject(r.config);
  if(p?.schema!==1 || !valid(p.printer,'machine') || !valid(p.process,'process') || !Array.isArray(p.filaments) || !p.filaments.every(r=>valid(r,'filament')))throw new Error('The saved profile references need repair. Their source settings have been kept.');
  return p;
}
