import type { State } from '../state/store';
import { selectCompatibility } from './compatibility';
import { effectiveProfileValues, preferredHeight } from './context';
import { captureProjectProfiles } from './project';
import { methodAvailability } from './method-context';
import { methodSelectionProblem } from './capabilities';
import type { Config, UserPreset } from '../api/types';
import { findProfile, profileKey, withProfileIdentity, type ProfileKey } from './identity';
import { catalogAnchor, changedKeys, resolveProfile, type ProfileCatalog } from './model';
import { emptyPrinterLibrary, emptyPrinterSetup, type PrinterProfileLibrary, type PrinterMaterialSetup, type ProfileRecovery } from './ownership';
import { SEP } from '../separator';

export interface MaterialLibraryState extends ProfileCatalog {
  printer: string;
  process?: string;
  filamentSlots: string[];
  slotColours: (string | undefined)[];
  filamentEdits: Record<string, Config>;
  mixes: PrinterMaterialSetup['mixes'];
  printerLibrary?: PrinterProfileLibrary;
}
export interface MaterialLibraryPlan {
  userPresets: UserPreset[];
  printerLibrary: PrinterProfileLibrary;
  recoveries: ProfileRecovery[];
  materialId?: ProfileKey;
}
export interface PrinterMaterialsPlan extends MaterialLibraryPlan {
  printer: ProfileKey;
  filamentSlots: string[];
  slotColours: (string | undefined)[];
  filamentEdits: Record<ProfileKey, Config>;
  mixes: PrinterMaterialSetup['mixes'];
  referenceMap: Record<string, ProfileKey>;
}
const libraryOf = (s: MaterialLibraryState) => s.printerLibrary ?? emptyPrinterLibrary();
const clone = <T>(value: T): T => structuredClone(value);
const putSetup = (library: PrinterProfileLibrary, setup: PrinterMaterialSetup): PrinterProfileLibrary => ({
  ...library, revision: library.revision + 1,
  setups: { ...library.setups, [setup.printerId]: { ...setup, revision: setup.revision + 1 } },
});
export function planAddMaterial(s: MaterialLibraryState, sourceKey: string, options: {
  printerId: ProfileKey; operationId: string; duplicate?: boolean; allowCrossPrinter?: boolean;
}): MaterialLibraryPlan {
  const library = libraryOf(s), owner = findProfile(s, 'machine', options.printerId);
  const unchanged = (recovery: ProfileRecovery): MaterialLibraryPlan => ({ userPresets: s.userPresets, printerLibrary: library, recoveries: [recovery] });
  if (!owner || profileKey(owner) !== options.printerId)
    return unchanged({ type: 'machine', key: options.printerId, reason: 'missing-owner' });
  const setup = library.setups[options.printerId] ?? emptyPrinterSetup(options.printerId);
  const retry = s.userPresets.find(p => p.type === 'filament' && p.printerId === options.printerId && p.provenance?.operationId === options.operationId);
  const source = findProfile(s, 'filament', sourceKey, options.printerId) ?? findProfile(s, 'filament', sourceKey);
  const resolved = resolveProfile(s, 'filament', source ? profileKey(source) : sourceKey);
  if (!retry && resolved.issue && !resolved.source?.snapshot)
    return unchanged({ type: 'filament', key: sourceKey, reason: resolved.issue.code });
  const reusable = retry ?? (!options.duplicate && source ? source : undefined);
  let material = reusable;
  if (!material) {
    const anchor = resolved.base ? catalogAnchor(resolved.base) : source?.snapshot?.base;
    if (!anchor) return unchanged({ type: 'filament', key: sourceKey, reason: 'missing-base', snapshot: source?.snapshot });
    const config = clone(resolved.config);
    const overlay = resolved.base ? changedKeys(config, resolved.base.config) : config;
    material = {
      id: `profile:${crypto.randomUUID()}`, type: 'filament', kind: 'profile',
      name: source?.name ?? resolved.name ?? sourceKey, revision: 0, base: clone(anchor), inherits: anchor.name,
      overlay, overrides: overlay, baseline: Object.fromEntries(Object.keys(overlay).map(key => [key, resolved.base?.config[key] ?? null])),
      metadata: source?.metadata && clone(source.metadata), snapshot: { config, base: clone(anchor) },
      provenance: { source: source ? 'profile' : 'catalog', sourceId: source && profileKey(source), sourceName: resolved.name ?? sourceKey,
        sourceRevision: source?.revision, operationId: options.operationId },
    };
  }
  const id = profileKey(material), included = setup.materialIds.includes(id);
  return { userPresets: reusable ? s.userPresets : [...s.userPresets, material], materialId: id,
    printerLibrary: included ? library : putSetup(library, { ...setup, materialIds: [...setup.materialIds, id] }),
    recoveries: resolved.issue ? [{ type: 'filament', key: id, reason: resolved.issue.code, snapshot: material.snapshot }] : [],
  };
}

export function capturePrinterMaterials(s: MaterialLibraryState): PrinterProfileLibrary {
  const library = libraryOf(s), printer = findProfile(s, 'machine', s.printer);
  if (!printer) return library;
  const id = profileKey(printer), setup = library.setups[id] ?? emptyPrinterSetup(id);
  const slots = s.filamentSlots.map((key, index) => ({ ...setup.slots[index], id: setup.slots[index]?.id ?? `slot:${crypto.randomUUID()}`,
    materialId: findProfile(s,'filament',key) ? profileKey(findProfile(s, 'filament', key)!) : key, colour: undefined }));
  const tuning = { ...setup.tuning, ...Object.fromEntries(Object.entries(s.filamentEdits).flatMap(([key, config]) => {
    const material = findProfile(s, 'filament', key); return material ? [[profileKey(material), clone(config)]] : [];
  })) };
  for (const slot of slots) if (!Object.keys(s.filamentEdits[slot.materialId] ?? {}).length) delete tuning[slot.materialId];
  const next = { ...setup, ...(s.process ? {processId:findProfile(s,'process',s.process)?.id ?? s.process} : {}), slots, materialIds: [...new Set([...setup.materialIds, ...slots.map(slot => slot.materialId)])], tuning, mixes: clone(s.mixes) };
  if (JSON.stringify({ ...next, revision: 0 }) === JSON.stringify({ ...setup, revision: 0 })) return library;
  return putSetup(library, next);
}

export function planPrinterSwitch(s: MaterialLibraryState, printerKey: string): PrinterMaterialsPlan {
  const library = capturePrinterMaterials(s), printer = findProfile(s, 'machine', printerKey), id = printer && profileKey(printer);
  const setup = id && library.setups[id];
  const result: PrinterMaterialsPlan = { userPresets: s.userPresets, printerLibrary: library, printer: id ?? printerKey,
    filamentSlots: setup ? setup.slots.map(slot => slot.materialId) : s.filamentSlots.length ? [...s.filamentSlots] : [''], slotColours: setup ? [] : [...s.slotColours],
    filamentEdits: setup ? clone(setup.tuning) : {}, mixes: setup ? clone(setup.mixes) : clone(s.mixes), referenceMap: {}, recoveries: [] };
  if (!id) result.recoveries.push({ type: 'machine', key: printerKey, reason: 'missing-owner' });
  if (setup) for (const key of setup.materialIds) if (!findProfile(s, 'filament', key))
    result.recoveries.push({ type: 'filament', key, reason: 'missing-profile' });
  return result;
}
export function planRemoveMaterialSlot(library: PrinterProfileLibrary, printerId: string, slotId: string): PrinterProfileLibrary {
  const setup = library.setups[printerId];
  if (!setup?.slots.some(slot => slot.id === slotId)) return library;
  return putSetup(library, { ...setup, slots: setup.slots.filter(slot => slot.id !== slotId),
    assignments: Object.fromEntries(Object.entries(setup.assignments).filter(([, slot]) => slot !== slotId)) });
}
export function renameOwnedProfile(profile: UserPreset, name: string): UserPreset {
  return { ...withProfileIdentity(profile), name, aliases: [...new Set([...(profile.aliases ?? []), profile.name])], revision: (profile.revision ?? 0) + 1 };
}

export interface ProfileTransitionSummary {
  changes:{kind:'process'|'filament';slot?:number;from:string;to?:string;reason:string}[];
  needsSelection:boolean;
}
const scalar=(value:Config[string]|undefined)=>String(Array.isArray(value)?value[0] ?? '':value ?? '');
export function reconcilePrinterSelection(previous:State,incoming:State,{definiteOnly=false}={}):{patch:Partial<State>;summary:ProfileTransitionSummary}{
  let next={...incoming,filamentSlots:[...incoming.filamentSlots]},summary:ProfileTransitionSummary={changes:[],needsSelection:false};
  const oldProcess=effectiveProfileValues(previous,'process',previous.process),base=resolveProfile(next,'machine',next.printer).base;
  const availability=methodAvailability(next),label=(type:'process'|'filament',key:string)=>resolveProfile(next,type,key).name ?? key;
  const kept=(c:{ok:boolean;unknown?:boolean})=>c.ok || definiteOnly && !!c.unknown;
  const methodKept=(problem:ReturnType<typeof methodSelectionProblem>)=>!problem || problem.recovery==='update_engine';
  const processValid=(key:string,raw=false,strict=false)=>(strict?selectCompatibility(next,'process',key).ok:kept(selectCompatibility(next,'process',key))) && methodKept(methodSelectionProblem(availability,raw?resolveProfile(next,'process',key).config:effectiveProfileValues(next,'process',key)));
  const setup=next.printerLibrary.setups[next.printer],remembered=setup?.processId;
  if(remembered && !processValid(next.process) && processValid(remembered))next.process=remembered;
  if(!processValid(next.process)){
    const candidates=next.userPresets.filter(p=>p.type==='process' && !p.archived && p.kind!=='master' && processValid(profileKey(p),true,true));
    const standard=scalar(base?.config.default_print_profile),height=Number(scalar(oldProcess.layer_height));
    const rank=(p:UserPreset)=>{const r=resolveProfile(next,'process',profileKey(p)),c=r.config;return [r.base?.name===standard?0:1,scalar(c.nonplanar_mode)===scalar(oldProcess.nonplanar_mode)?0:1,scalar(c.nonplanar_wave)===scalar(oldProcess.nonplanar_wave)?0:1,Number.isFinite(height)?Math.abs(Number(scalar(c.layer_height))-height):0];};
    candidates.sort((a,b)=>{const x=rank(a),y=rank(b);for(let i=0;i<x.length;i++)if(Number.isFinite(x[i]-y[i])&&x[i]!==y[i])return x[i]-y[i];return next.userPresets.indexOf(a)-next.userPresets.indexOf(b);});
    const chosen=candidates[0];summary.changes.push({kind:'process',from:label('process',next.process),to:chosen?.name,reason:chosen?'Compatible print settings selected':'Choose compatible print settings'});
    if(chosen){next.process=profileKey(chosen);next.dirty={...next.dirty,process:{}};if(next.projectPresets)next.projectPresets={...next.projectPresets,process:{}};if(next.profileSnapshots)next.profileSnapshots={...next.profileSnapshots,process:captureProjectProfiles({...next,profileSnapshots:null,projectPresets:null}).process};}
    else summary.needsSelection=true;
  }
  const defaults=[base?.config.default_filament_profile ?? []].flat().map(String);
  const materialValid=(key:string)=>kept(selectCompatibility(next,'filament',key));
  next.filamentSlots.forEach((key,slot)=>{
    if(!key || materialValid(key))return;
    const current=resolveProfile(next,'filament',key),material=scalar(current.config.filament_type),brand=scalar(current.config.filament_vendor);
    const candidates=next.userPresets.filter(p=>p.type==='filament'&&!p.archived&&p.kind!=='master'&&selectCompatibility(next,'filament',profileKey(p)).ok);
    const sameMaterial=(p:UserPreset)=>!material||scalar(resolveProfile(next,'filament',profileKey(p)).config.filament_type)===material;
    candidates.sort((a,b)=>{const x=resolveProfile(next,'filament',profileKey(a)),y=resolveProfile(next,'filament',profileKey(b));
      return Number(!sameMaterial(a))-Number(!sameMaterial(b))||Number(!defaults.includes(x.base?.name ?? ''))-Number(!defaults.includes(y.base?.name ?? ''))
        ||Number(scalar(x.config.filament_vendor)!==brand)-Number(scalar(y.config.filament_vendor)!==brand)||profileKey(a).localeCompare(profileKey(b));});
    const chosen=candidates[0];summary.changes.push({kind:'filament',slot,from:label('filament',key),to:chosen?.name,reason:chosen?'Compatible filament selected':`Choose compatible ${material || 'filament'}`});
    if(chosen){next.filamentSlots[slot]=profileKey(chosen);if(next.projectPresets)next.projectPresets={...next.projectPresets,filaments:next.projectPresets.filaments.map((config,i)=>i===slot?{}:config)};if(next.profileSnapshots){const rows=[...next.profileSnapshots.filaments];rows[slot]=captureProjectProfiles({...next,profileSnapshots:null,projectPresets:null}).filaments[slot];next.profileSnapshots={...next.profileSnapshots,filaments:rows};}}
    else summary.needsSelection=true;
  });
  if(next.process!==previous.process && !summary.changes.some(change=>change.kind==='process'))summary.changes.push({kind:'process',from:resolveProfile(previous,'process',previous.process).name ?? previous.process,to:label('process',next.process),reason:'Restored printer print settings'});
  next.filamentSlots.forEach((key,slot)=>{if(key!==previous.filamentSlots[slot]&&!summary.changes.some(change=>change.kind==='filament'&&change.slot===slot))summary.changes.push({kind:'filament',slot,from:resolveProfile(previous,'filament',previous.filamentSlots[slot] ?? '').name ?? previous.filamentSlots[slot] ?? '',to:label('filament',key),reason:'Restored loaded filament'});});
  const contexts={...previous.projectProfileContexts,[previous.printer]:{projectPresets:previous.projectPresets,profileSnapshots:captureProjectProfiles(previous),process:previous.process,dirty:previous.dirty}};
  const preferred=next.process!==previous.process ? preferredHeight(next,next.process) : undefined;
  return{patch:{...(preferred?{processVariant:{layerHeight:preferred}}:{}),process:next.process,filamentSlots:next.filamentSlots,dirty:next.dirty,projectPresets:next.projectPresets,profileSnapshots:next.profileSnapshots,projectProfileContexts:contexts,lastProfileTransition:summary},summary};
}
export function selectionNotice(s:State,summary:ProfileTransitionSummary):string{
  const printer=resolveProfile(s,'machine',s.printer).name ?? s.printer;
  return summary.changes.filter(c=>c.to && c.from && c.reason!=='Restored loaded filament' && c.reason!=='Restored printer print settings')
    .map(c=>`${c.kind==='process'?'Print settings':s.filamentSlots.length>1?`Slot ${(c.slot??0)+1} filament`:'Filament'} changed to ${c.to}: ${c.from} isn't compatible with ${printer}`)
    .concat(summary.needsSelection?[`Choose ${summary.changes.some(c=>c.kind==='process'&&!c.to)?'print settings':'a filament'} compatible with ${printer}`]:[]).join(SEP);
}
export function ensureCompatibleSelection(s:State):{patch:Partial<State>;notice:string}|null{
  if(s.profileLibraryLoad.state!=='ready' || !s.printer)return null;
  const {patch,summary}=reconcilePrinterSelection(s,s,{definiteOnly:true});
  const changed=patch.process!==s.process || patch.filamentSlots!.some((key,i)=>key!==s.filamentSlots[i]);
  if(!changed && !summary.needsSelection)return null;
  const {projectProfileContexts:_contexts,...rest}=patch;
  return {patch:changed?rest:{lastProfileTransition:summary},notice:selectionNotice({...s,...rest},summary)};
}
