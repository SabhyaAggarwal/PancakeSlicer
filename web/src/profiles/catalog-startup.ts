import { beginBusy, startupBusy } from '../state/busy';
import type { PresetCatalog, PresetType, ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import type { Api } from '../api/client';
import type { StoredDoc } from '../persist/session';
import type { ProfileBaseAnchor, ProfileLibraryDocument, ProjectProfileSnapshot } from './ownership';
import { app } from '../flow/app';
import { importing } from '../flow/import';
import { loadTab } from '../options';
import { getState, setState, toast } from '../state/store';
import { documentGeneration } from '../state/history';
import { initialiseSavedPreferences, preferenceText } from '../saved-preferences';
import { findProfile } from './identity';
import { catalogAnchor, catalogKey, familyVariantName, findCatalogBase, type ProfileCatalog } from './model';
import { filamentCatalog } from './catalog-reads';
import { ensureCompatibleSelection } from './material-library';
import type { CatalogBaseDependency, CatalogDependencySource } from './catalog-readiness';
const VENDORS = ['BBL', 'Prusa', 'Custom'];

function mergeCatalog(_vendor: string, cat: PresetCatalog) {
  const order = <T extends { vendor: string }>(l: T[]) => [...l].sort((a, b) => VENDORS.indexOf(a.vendor) - VENDORS.indexOf(b.vendor));
  const merge=(current:SystemPreset[],incoming:SystemPreset[])=>{
    const keys=new Set(incoming.map(catalogKey));
    return order([...current.filter(p=>!keys.has(catalogKey(p))),...incoming]);
  };
  setState(s=>({
    machines:merge(s.machines,cat.machines),
    processes:merge(s.processes,cat.processes),
    filaments:[...new Map(merge(s.filaments,cat.filaments).map(p=>[catalogKey(p),p])).values()],
  }));
}

let catalogLoadGeneration = 0;
const transient = (error: unknown) => error instanceof TypeError || [408, 429].includes((error as {status?:number}).status ?? 0) || ((error as {status?:number}).status ?? 0) >= 500;
let retryDelay = 1000, armed: (() => void) | null = null;
function disarmRetry() { armed?.(); armed = null; }
function retryCatalog(generation: number) {
  disarmRetry();
  const again = () => { disarmRetry(); if (generation === catalogLoadGeneration) void loadCatalog(); };
  const timer = setTimeout(again, retryDelay); window.addEventListener('online', again);
  armed = () => { clearTimeout(timer); window.removeEventListener('online', again); };
  retryDelay = Math.min(retryDelay * 2, 10_000);
}
let catalogLibraryTail: Promise<void> = Promise.resolve();
export async function loadCatalog() {
  disarmRetry();
  const generation = ++catalogLoadGeneration, initial = getState(), api = initial.api, documentAtStart=documentGeneration();
  if (!api) return;
  const busy = beginBusy('Loading presets');
  startupBusy.end();
  setState({ profileLibraryLoad: {state: 'loading'} });
  try {
  const current = () => generation === catalogLoadGeneration && getState().api === api;
  const native=session=createSession(api,current);
  const settings = loadTab('process');
  const library = catalogLibraryTail.then(async () => {
    try {
      const [storage,{prepareProfileLibraryHydration},{acceptHydratedPrinterSetups},{acknowledgePendingProfileWrite}]=await Promise.all([
        import('../persist/profile-library'),import('./library-hydration'),import('./printer-setups'),import('./transactions')]);
      await acknowledgePendingProfileWrite().catch(error=>{if((error as {code?:string}).code!=='library_reset')throw error;});
      if(!current())return;
      const [document]=await Promise.all([storage.readProfileLibrary(),settings]);
      if(!current())return;
      const unchanged=()=>{const s=getState();return current() && documentGeneration()===documentAtStart && s.project===initial.project && s.profileSnapshots===initial.profileSnapshots && s.projectPresets===initial.projectPresets && s.dirty===initial.dirty && (s.userPresets===initial.userPresets || s.userPresets.length===initial.userPresets.length && s.userPresets.every((p,i)=>p===initial.userPresets[i])) && s.printer===initial.printer && s.process===initial.process && s.filamentSlots===initial.filamentSlots && s.slotColours===initial.slotColours && s.filamentEdits===initial.filamentEdits && s.mixes===initial.mixes && s.printerLibrary===initial.printerLibrary;};
      const restoreStartupPlaceholder=!initial.printer && !initial.process && !initial.filamentSlots.length && !initial.project && !initial.objects.length && !initial.profileSnapshots && !initial.projectPresets && !Object.keys(initial.dirty.machine).length && !Object.keys(initial.dirty.process).length && !Object.keys(initial.filamentEdits).length && !Object.keys(initial.printerLibrary.setups).length;
      if(!current())return;
      if(!unchanged())throw new Error('Your setup changed while profiles were loading. We kept your newest changes—just reload profiles to sync up.');
      await ensureOwnedCatalogs(document,native,unchanged);
      if(!unchanged())throw new Error('Your setup changed while built-in profiles were loading. Click Retry so you don\'t lose your latest changes.');
      if(restoreStartupPlaceholder){
        busy.end();
        await initialiseSavedPreferences();
      }
      if(!unchanged())throw new Error('Your setup changed while preferences were loading. Click Retry so you don\'t lose your latest changes.');
      const latest=getState(),preferred=restoreStartupPlaceholder ? preferenceText('orca.printer') : undefined;
      const remembered=document.profiles.find(p=>p.type==='machine' && p.id===preferred && p.kind!=='master');
      const result=prepareProfileLibraryHydration(preferred ? {...latest,printer:preferred} : latest,document,{restoreStartupPlaceholder});
      if(restoreStartupPlaceholder && (!remembered || remembered.archived)){
        Object.assign(result.patch,{printer:remembered?.id ?? '',process:latest.process,filamentSlots:latest.filamentSlots,
          slotColours:latest.slotColours,filamentEdits:latest.filamentEdits,mixes:latest.mixes,
          profileRecoveries:remembered?.archived?[{type:'machine',key:remembered.id,reason:'archived'}]:[]});
      }
      const error=result.requiresAdoption?'You have unsaved changes to your printer setup. Save or discard them first, then reload your profiles.':null;
      setState({...result.patch,profileLibraryLoad:error ? {state:'failed',error} : {state:'ready'}});
      if(!error){
        acceptHydratedPrinterSetups();retryDelay=1000;
        const fixed=ensureCompatibleSelection(getState());
        if(fixed){setState(fixed.patch);if(fixed.notice)toast(fixed.notice,null,undefined,'warning');}
      }
    } catch (error) {
      if(current()){
        setState({profileLibraryLoad:{state:'failed',error:error instanceof Error?error.message:String(error)}});
        if(transient(error))retryCatalog(generation);
      }
    }
  });
  catalogLibraryTail=library;
  await library;
  if (!current()) return;
  busy.end();
  app.send({type: 'READY', hasObjects: getState().objects.length > 0, importing: importing.machine.get().value === 'running'});
  } finally { busy.end(); }
}


interface CatalogSession {
  api: Api;
  current(): boolean;
  reads: Map<string, Promise<void>>;
  loadNames(vendor: string, names: string[]): Promise<void>;
}
let session: CatalogSession | null = null;
function createSession(api: Api, current: () => boolean): CatalogSession {
  const captured: CatalogSession = {api,current,reads:new Map(),loadNames(vendor,names) {
    const key=`${vendor}\n${[...names].sort().join('\n')}`,previous=captured.reads.get(key);if(previous)return previous;
    const read=(async()=>{
      const cat=await api.presets(vendor,[],value=>{if(current())mergeCatalog(vendor,value);},names);
      if(current())mergeCatalog(vendor,cat);
    })().catch(error=>{captured.reads.delete(key);throw error;});
    captured.reads.set(key,read);return read;
  }};
  return captured;
}
function withImportedCatalog(document?: ProfileLibraryDocument): ProfileCatalog {
  const s=getState(),extra=document?.catalog;
  if(!extra?.length)return s;
  const merge=(current:SystemPreset[],type:PresetType)=>{
    const keys=new Set(current.map(catalogKey));
    return [...current,...extra.filter(p=>p.type===type && !keys.has(catalogKey(p)))];
  };
  return {...s,machines:merge(s.machines,'machine'),processes:merge(s.processes,'process'),filaments:merge(s.filaments,'filament')};
}
function dependencies(catalog: ProfileCatalog) {
  const bases:CatalogBaseDependency[]=[],visited=new Set<object>();let complete=true;
  const add=(type:PresetType,base:ProfileBaseAnchor|undefined)=>{
    if(!base?.name){complete=false;return;}bases.push({type,base});
  };
  const record=(profile:UserPreset|ProfileOverlay)=>{
    if(visited.has(profile))return;visited.add(profile);
    add(profile.type,profile.base ?? ('inherits' in profile?{name:profile.inherits,version:''}:undefined));
    for(const key of [profile.parentId,profile.parent,profile.masterId])if(key)reference(profile.type,key);
    if(profile.snapshot)add(profile.type,profile.snapshot.base);
  };
  const reference=(type:PresetType,key:unknown)=>{
    if(typeof key!=='string' || !key){complete=false;return;}
    const owned=findProfile(catalog,type,key);if(owned){record(owned);return;}
    const found=findCatalogBase(catalog,type,key);
    if(found.base)add(type,catalogAnchor(found.base));else add(type,{name:key,version:''});
  };
  const snapshot=(row:ProjectProfileSnapshot|undefined,type:PresetType)=>{
    if(!row){complete=false;return;}
    add(type,row.base);if(row.profile)record(row.profile);
  };
  const references=(value:StoredDoc['profileSnapshots'])=>{
    if(!value){complete=false;return;}
    snapshot(value.printer,'machine');snapshot(value.process,'process');
    if(!Array.isArray(value.filaments)){complete=false;return;}value.filaments.forEach(row=>snapshot(row,'filament'));
    value.slots?.forEach(slot=>{if(slot.snapshot)snapshot(slot.snapshot,'filament');});
  };
  return {record,reference,snapshot,references,source:():CatalogDependencySource=>({complete,bases})};
}
async function ensure(source:CatalogDependencySource,captured:CatalogSession,isCurrent:()=>boolean,document?:ProfileLibraryDocument) {
  if(!isCurrent())return;
  if(!captured.current())throw new Error('The profile catalog updated in the background. Try restoring this project again.');
  const bases=source.bases.filter(d=>!(d.type==='process' && d.base.name==='Auto'));
  const available=(d:CatalogBaseDependency)=>!!findCatalogBase(withImportedCatalog(document),d.type,d.base).base;
  const missing=bases.filter(d=>!available(d));
  {
    const byVendor=(list:{vendor:string;name:string}[])=>{const m=new Map<string,Set<string>>();for(const {vendor,name} of list)m.set(vendor,(m.get(vendor) ?? new Set()).add(name));return m;};
    const read=(m:Map<string,Set<string>>)=>Promise.all([...m].map(([vendor,names])=>captured.loadNames(vendor,[...names])));
    await read(byVendor(missing.flatMap(d=>d.base.vendor ? [{vendor:d.base.vendor,name:d.base.name}] : [])));
    const catalog=withImportedCatalog(document),standards=bases.flatMap(d=>{
      const machine=d.type==='machine' ? findCatalogBase(catalog,'machine',d.base).base as (SystemPreset & {standard_process?:string}) | undefined : undefined;
      return machine?.standard_process && !catalog.processes.some(p=>p.name===machine.standard_process) ? [{vendor:machine.vendor,name:machine.standard_process}] : [];
    });
    await read(byVendor(standards));
  }
  if(isCurrent() && !captured.current())throw new Error('The profile catalog updated in the background. Try restoring this project again.');
}
async function ensureOwnedCatalogs(document:ProfileLibraryDocument,captured:CatalogSession,isCurrent:()=>boolean) {
  const catalog=withImportedCatalog(document),collector=dependencies({...catalog,userPresets:document.profiles as UserPreset[]});
  document.profiles.forEach(collector.record);
  for(const setup of Object.values(document.printers.setups))for(const slot of setup.slots)if(slot.snapshot)collector.snapshot(slot.snapshot,'filament');
  await ensure(collector.source(),captured,isCurrent,document);
  if(isCurrent())await loadFilamentVariants(document.profiles,document.profiles.flatMap(p=>p.type==='machine' && !p.archived && p.base?.name ? [p.base.name] : []));
}
export async function ensureProjectCatalogs(document:StoredDoc,isCurrent:()=>boolean):Promise<void> {
  const captured=session;if(!captured)throw new Error('Hang on a second—wait for the profile catalog to finish loading before restoring this project.');
  if(!isCurrent())return;
  const collector=dependencies(getState());
  if(document.profileSnapshots)collector.references(document.profileSnapshots);
  else {collector.reference('machine',document.printer);collector.reference('process',document.process);document.filamentSlots?.forEach(key=>collector.reference('filament',key));}
  for(const [printer,context] of Object.entries(document.projectProfileContexts ?? {})){
    collector.reference('machine',printer);collector.reference('process',context.process);
    if(context.profileSnapshots)collector.references(context.profileSnapshots);
  }
  await ensure(collector.source(),captured,isCurrent);
}
export async function loadMachinePresets(vendor:string,machines:string[]):Promise<void> {
  const captured=session;if(!captured)throw new Error('Hang on a second—wait for the profile catalog to finish loading before switching nozzles.');
  const missing=machines.filter(name=>!findCatalogBase(getState(),'machine',{name,vendor,version:''}).base);
  if(missing.length)await captured.loadNames(vendor,missing);
  const standards=machines.flatMap(name=>{const m=findCatalogBase(getState(),'machine',{name,vendor,version:''}).base as (SystemPreset & {standard_process?:string}) | undefined;
    return m?.standard_process && !getState().processes.some(p=>p.name===m.standard_process) ? [m.standard_process] : [];});
  if(standards.length)await captured.loadNames(vendor,standards);
}
export async function loadFilamentVariants(profiles:readonly (UserPreset|ProfileOverlay)[],machines:string[]):Promise<void> {
  const captured=session;if(!captured)throw new Error('Hang on a second—wait for the profile catalog to finish loading before browsing filaments.');
  const byVendor=new Map<string,Set<string>>();
  for(const f of profiles){
    const family=String(f.metadata?.family ?? '');
    if(f.type!=='filament' || f.archived || !family)continue;
    const vendor=family.split('/')[0];if(!(await filamentCatalog(vendor).catch(()=>null)))continue;
    for(const machine of machines){
      const name=familyVariantName(family,machine);
      if(name && !findCatalogBase(getState(),'filament',{name,vendor,version:''}).base)byVendor.set(vendor,(byVendor.get(vendor) ?? new Set()).add(name));
    }
  }
  await Promise.all([...byVendor].map(([vendor,names])=>captured.loadNames(vendor,[...names])));
}
export async function ensureLibraryPresets():Promise<void> {
  const captured=session;if(!captured)return;
  const s=getState();
  await ensureOwnedCatalogs({schema:1,revision:s.profileLibraryRevision,profiles:s.userPresets as unknown as ProfileOverlay[],printers:s.printerLibrary},captured,()=>captured.current())
    .catch(error=>toast(`Oops! Couldn't load profiles: ${error instanceof Error?error.message:String(error)}`));
}
