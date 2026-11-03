import type { PresetCatalog, PresetType, ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import type { Api } from '../api/client';
import type { StoredDoc } from '../persist/session';
import type { ProfileBaseAnchor, ProfileLibraryDocument, ProjectProfileSnapshot } from './ownership';
import { app } from '../flow/app';
import { importing } from '../flow/import';
import { loadTab } from '../options';
import { DEMO_MODELS, getState, setState, toast } from '../state/store';
import { documentGeneration } from '../state/history';
import { initialiseSavedPreferences, preferenceText } from '../saved-preferences';
import { findProfile } from './identity';
import { catalogAnchor, catalogKey, findCatalogBase, type ProfileCatalog } from './model';
import { ensureCompatibleSelection } from './material-library';
import { planCatalogReadiness, loadRequiredCatalogs, loadBackgroundCatalogs, type CatalogBaseDependency, type CatalogDependencySource } from './catalog-readiness';
const VENDORS = ['BBL', 'Prusa', 'Custom'];

function mergeCatalog(vendor: string, cat: PresetCatalog) {
  const order = <T extends { vendor: string }>(l: T[]) => [...l].sort((a, b) => VENDORS.indexOf(a.vendor) - VENDORS.indexOf(b.vendor));
  const merge=(current:SystemPreset[],incoming:SystemPreset[])=>{
    const keys=new Set(incoming.map(catalogKey));
    return order([...current.filter(p=>(p.imported || p.vendor!==vendor) && !keys.has(catalogKey(p))),...incoming]);
  };
  setState(s=>({
    machines:merge(s.machines,cat.machines),
    processes:merge(s.processes,cat.processes),
    filaments:[...new Map(merge(s.filaments,cat.filaments).map(p=>[catalogKey(p),p])).values()],
  }));
}

let catalogLoadGeneration = 0;
const transient = (error: unknown) => error instanceof TypeError || [408, 429].includes((error as {status?:number}).status ?? 0) || ((error as {status?:number}).status ?? 0) >= 500;
let retryDelay = 1000;
function retryCatalog(generation: number) {
  const again = () => { clearTimeout(timer); window.removeEventListener('online', again); if (generation === catalogLoadGeneration) void loadCatalog(); };
  const timer = setTimeout(again, retryDelay); window.addEventListener('online', again);
  retryDelay = Math.min(retryDelay * 2, 10_000);
}
let catalogLibraryTail: Promise<void> = Promise.resolve();
export async function loadCatalog() {
  const generation = ++catalogLoadGeneration, initial = getState(), api = initial.api, documentAtStart=documentGeneration();
  if (!api) return;
  setState({ loading: 'Loading presets', profileLibraryLoad: {state: 'loading'} });
  let first = 'BBL';
  first = preferenceText('orca.vendor') || first;
  const current = () => generation === catalogLoadGeneration && getState().api === api;
  const native=session=createSession(api,current);
  void native.load(first).catch(error=>{if(current())toast(`Catalog ${first} could not load: ${error instanceof Error?error.message:String(error)}`);});
  const settings = loadTab('process');
  const library = catalogLibraryTail.then(async () => {
    try {
      const [storage,{prepareProfileLibraryHydration},{acceptHydratedPrinterSetups},{acknowledgePendingProfileWrite}]=await Promise.all([
        import('../persist/profile-library'),import('./library-hydration'),import('./printer-setups'),import('./transactions')]);
      await acknowledgePendingProfileWrite().catch(error=>{if((error as {code?:string}).code!=='library_reset')throw error;});
      if(!current())return;
      const [document]=await Promise.all([storage.readProfileLibrary(),settings]);
      if(!current())return;
      void import('./service-recovery').then(m=>m.refreshProfileRecovery());
      const unchanged=()=>{const s=getState();return current() && documentGeneration()===documentAtStart && s.project===initial.project && s.profileSnapshots===initial.profileSnapshots && s.projectPresets===initial.projectPresets && s.dirty===initial.dirty && (s.userPresets===initial.userPresets || s.userPresets.length===initial.userPresets.length && s.userPresets.every((p,i)=>p===initial.userPresets[i])) && s.printer===initial.printer && s.process===initial.process && s.filamentSlots===initial.filamentSlots && s.slotColours===initial.slotColours && s.filamentEdits===initial.filamentEdits && s.mixes===initial.mixes && s.printerLibrary===initial.printerLibrary;};
      const restoreStartupPlaceholder=!initial.printer && !initial.process && !initial.filamentSlots.length && !initial.project && !initial.objects.length && !initial.profileSnapshots && !initial.projectPresets && !Object.keys(initial.dirty.machine).length && !Object.keys(initial.dirty.process).length && !Object.keys(initial.filamentEdits).length && !Object.keys(initial.printerLibrary.setups).length;
      if(!current())return;
      if(!unchanged())throw new Error('Your setup changed while profiles loaded. Your newer work is kept; reload profiles.');
      await ensureOwnedCatalogs(document,native,unchanged);
      if(!unchanged())throw new Error('Your setup changed while native profile bases loaded. Retry to preserve your newer work.');
      if(restoreStartupPlaceholder){
        if(getState().loading==='Loading presets')setState({loading:null});
        await initialiseSavedPreferences();
      }
      if(!unchanged())throw new Error('Your setup changed while saved preferences loaded. Retry to preserve your newer work.');
      const latest=getState(),preferred=restoreStartupPlaceholder ? preferenceText('orca.printer') : undefined;
      const remembered=document.profiles.find(p=>p.type==='machine' && p.id===preferred && p.kind!=='master');
      const result=prepareProfileLibraryHydration(preferred ? {...latest,printer:preferred} : latest,document,{restoreStartupPlaceholder});
      if(restoreStartupPlaceholder && (!remembered || remembered.archived)){
        Object.assign(result.patch,{printer:remembered?.id ?? '',process:latest.process,filamentSlots:latest.filamentSlots,
          slotColours:latest.slotColours,filamentEdits:latest.filamentEdits,mixes:latest.mixes,
          profileRecoveries:remembered?.archived?[{type:'machine',key:remembered.id,reason:'archived'}]:[]});
      }
      const error=result.requiresAdoption?'Your printer setup has unsaved changes that differ from the saved one. Save or discard them, then reload profiles.':null;
      setState({...result.patch,profileLibraryLoad:error ? {state:'failed',error} : {state:'ready'}});
      if(!error){
        acceptHydratedPrinterSetups();retryDelay=1000;
        const fixed=ensureCompatibleSelection(getState());
        if(fixed){setState(fixed.patch);if(fixed.notice)toast(fixed.notice);}
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
  setState({loading: null});
  app.send({type: 'READY', hasObjects: getState().objects.length > 0, importing: importing.machine.get().value === 'running'});
  if(projectsSettled)void startUnrelatedCatalogs();
}


interface CatalogSession {
  api: Api;
  current(): boolean;
  vendors: string[];
  reads: Map<string, Promise<void>>;
  loaded: Set<string>;
  load(vendor: string): Promise<void>;
}
let session: CatalogSession | null = null;
let projectsSettled = false;
function createSession(api: Api, current: () => boolean): CatalogSession {
  const captured: CatalogSession = {api,current,vendors:[...VENDORS],reads:new Map(),loaded:new Set(),load(vendor) {
    const previous=captured.reads.get(vendor);if(previous)return previous;
    const read=(async()=>{
      const cat=await api.presets(vendor,DEMO_MODELS,value=>{if(current())mergeCatalog(vendor,value);});
      if(current()){mergeCatalog(vendor,cat);captured.loaded.add(vendor);}
    })().catch(error=>{captured.reads.delete(vendor);throw error;});
    captured.reads.set(vendor,read);return read;
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
  if(!captured.current())throw new Error('The catalog source changed. Retry restoring this project.');
  for(const {base} of source.bases)if(base.vendor && !captured.vendors.includes(base.vendor))captured.vendors.push(base.vendor);
  const catalog=withImportedCatalog(document);
  const plan=planCatalogReadiness(captured.vendors,[source],dependency=>!!findCatalogBase(catalog,dependency.type,dependency.base).base);
  const completed=await loadRequiredCatalogs(plan,()=>captured.current() && isCurrent(),vendor=>captured.load(vendor));
  if(isCurrent() && (!completed || !captured.current()))throw new Error('The catalog source changed. Retry restoring this project.');
}
async function ensureOwnedCatalogs(document:ProfileLibraryDocument,captured:CatalogSession,isCurrent:()=>boolean) {
  const catalog=withImportedCatalog(document),collector=dependencies({...catalog,userPresets:document.profiles as UserPreset[]});
  document.profiles.forEach(collector.record);
  for(const setup of Object.values(document.printers.setups))for(const slot of setup.slots)if(slot.snapshot)collector.snapshot(slot.snapshot,'filament');
  await ensure(collector.source(),captured,isCurrent,document);
}
export async function ensureProjectCatalogs(document:StoredDoc,isCurrent:()=>boolean):Promise<void> {
  const captured=session;if(!captured)throw new Error('Load the profile catalog before restoring this project.');
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
export async function startUnrelatedCatalogs():Promise<void> {
  projectsSettled=true;
  const captured=session;if(!captured)return;
  const plan={required:[],background:captured.vendors.filter(v=>!captured.loaded.has(v)),missing:[],incomplete:false};
  await loadBackgroundCatalogs(plan,{current:captured.current,yield:()=>new Promise(resolve=>setTimeout(resolve,0)),load:vendor=>captured.load(vendor),
    failed:(vendor,error)=>toast(`Catalog ${vendor} could not load: ${error instanceof Error?error.message:String(error)}`,{label:'Retry',run:()=>void startUnrelatedCatalogs()})});
}
