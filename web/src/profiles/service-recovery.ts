import { getState, setState, toast } from '../state/store';
import { documentGeneration } from '../state/history';
import { LegacyProfilesPendingError, persistProfileLibrary, readProfileLibraryAdoption, readProfileLibraryAdoptions, readProfileLibrary, requireServiceProfileAdoption } from '../persist/profile-library';
import { inspectServiceProfileAdoption, planServiceProfileAdoption, type ServiceProfileReview, type ServiceProfilePlanningDecision } from './service-adoption';
import type { ProfileAdoptionPlan } from './profile-adoption';
import type { ProfileLibraryAdoption, ProfileLibraryDocument, ProfileLibraryWrite } from './ownership';

export type ServiceRecoverySnapshot = {state:'idle'} | {
  state:'loading'|'review'|'saving'|'complete'; pending:LegacyProfilesPendingError;
  profiles:readonly ServiceProfileReview[]; choices:Readonly<Record<string,string>>;
  decisions:readonly ServiceProfilePlanningDecision[];
  plan?:ProfileAdoptionPlan; error?:string; frozen:boolean; incoming?:LegacyProfilesPendingError;
};
let snapshot:ServiceRecoverySnapshot={state:'idle'},generation=0;
const listeners=new Set<()=>void>();
export const serviceRecoverySnapshot=()=>snapshot;
export const subscribeServiceRecovery=(listener:()=>void)=>{listeners.add(listener);return()=>{listeners.delete(listener);};};
const publish=(value:ServiceRecoverySnapshot)=>{snapshot=value;listeners.forEach(fn=>fn());};
const message=(error:unknown)=>error instanceof Error?error.message:String(error);
const sameSource=(a:ProfileLibraryAdoption,b:ProfileLibraryAdoption)=>a.sourceId===b.sourceId && a.digest===b.digest && a.source===b.source;
export function openSavedProfileRecovery():void {
  const ux=getState().presetUx;
  setState({presetUx:ux?.kind==='presets'?{...ux,add:false}:{kind:'presets',type:'machine'}});
  if(snapshot.state==='idle')void readProfileLibrary().catch(async error=>{
    if(error instanceof LegacyProfilesPendingError)await beginServiceRecovery(error);
  });
}
export function explainProfileWriteBlock():void {
  const load=getState().profileLibraryLoad;
  toast(load.state==='loading'?'Saved profiles are still loading. Your choices are kept.':'Review your saved profiles before adding or saving. Your current choices are kept.',
    {label:'Review saved profiles',run:openSavedProfileRecovery});
}
export async function handleProfileWriteError(error:unknown):Promise<boolean> {
  if(!(error instanceof LegacyProfilesPendingError) && (error as {code?:string})?.code!=='legacy_profiles_pending')return false;
  setState({profileLibraryLoad:{state:'failed',error:'Earlier profile files need a one-time review before new profiles can be saved. Original files and your edits are kept.',recovery:'adopt_service_profiles'}});
  try {
    if(error instanceof LegacyProfilesPendingError)await beginServiceRecovery(error);
    else try{await readProfileLibrary();}catch(pending){if(pending instanceof LegacyProfilesPendingError)await beginServiceRecovery(pending);}
  } finally {explainProfileWriteBlock();}
  return true;
}
function workingContext(){
  const before=getState(),document=documentGeneration();
  return ()=>{
    const now=getState();
    return documentGeneration()===document && now.api===before.api && now.printer===before.printer && now.process===before.process
      && now.objects===before.objects && now.project===before.project && now.projectName===before.projectName && now.events===before.events && now.bedType===before.bedType
      && now.filamentSlots===before.filamentSlots && now.slotColours===before.slotColours && now.filamentEdits===before.filamentEdits && now.mixes===before.mixes
      && now.projectPresets===before.projectPresets && now.dirty===before.dirty && now.profileSnapshots===before.profileSnapshots
      && now.printerLibrary===before.printerLibrary && now.projectProfileContexts===before.projectProfileContexts
      && (now.userPresets===before.userPresets || now.userPresets.length===before.userPresets.length && now.userPresets.every((p,i)=>p===before.userPresets[i]));
  };
}
let frozen:{write:ProfileLibraryWrite;current:()=>boolean}|undefined;
export async function beginServiceRecovery(pending:LegacyProfilesPendingError,replace=false):Promise<void>{
  const previous=snapshot.state!=='idle' && sameSource(snapshot.pending.adoption,pending.adoption)?snapshot.choices:{};
  const decisions=snapshot.state!=='idle' && sameSource(snapshot.pending.adoption,pending.adoption)?snapshot.decisions:[];
  if(snapshot.state!=='idle' && snapshot.state!=='complete'){
    if(sameSource(snapshot.pending.adoption,pending.adoption) && !replace)return;
    if(frozen || !replace){generation++;publish({...snapshot,state:snapshot.state==='saving'?'saving':'review',incoming:pending,error:'The original files changed. Keep the current choices and explicitly review the new inventory before saving.'});return;}
  }
  const mine=++generation;
  publish({state:'loading',pending,profiles:[],choices:previous,decisions,frozen:false});
  try{
    const [{profiles},receipts]=await Promise.all([inspectServiceProfileAdoption(pending.adoption),readProfileLibraryAdoptions()]);
    const choices:Record<string,string>={...previous};
    for(const receipt of [...receipts].sort((a,b)=>a.revision-b.revision))if(receipt.sourceId===pending.adoption.sourceId){
      for(const choice of receipt.identityResolutions ?? [])if(profiles.some(p=>!p.profile.id && p.path===choice.path && p.sha256===choice.sha256))choices[choice.path]=choice.profileId;
    }
    if(mine===generation)publish({state:'review',pending,profiles,choices,decisions,frozen:false});
  }catch(error){if(mine===generation)publish({state:'review',pending,profiles:[],choices:{},decisions,error:message(error),frozen:false});}
}
export function keepServiceProfileCurrent(path:string,keep:boolean):void{
  if(snapshot.state!=='review' || frozen || snapshot.incoming)return;
  const row=snapshot.profiles.find(p=>p.path===path),current=snapshot.pending.document.profiles.find(p=>p.id===row?.profile.id);
  if(!row?.profile.id || !current || row.profile.type!==current.type)return;
  generation++;
  const decisions=snapshot.decisions.filter(d=>d.path!==path);
  if(keep)decisions.push({path,sha256:row.sha256,choice:'keep-current',current:structuredClone(current)});
  publish({...snapshot,decisions,plan:undefined,error:undefined});
}
export function chooseServiceProfileIdentity(path:string,profileId:string):void{
  if(snapshot.state!=='review' || frozen || snapshot.incoming || !snapshot.profiles.some(p=>p.path===path && !p.profile.id))return;
  generation++;const choices={...snapshot.choices};
  if(profileId)choices[path]=profileId;else delete choices[path];
  publish({...snapshot,choices,plan:undefined,error:undefined});
}
export function createServiceProfileIdentity(path:string):void{
  if(snapshot.state!=='review' || frozen || snapshot.incoming)return;
  const id=snapshot.choices[path];
  if(id && !snapshot.pending.document.profiles.some(p=>p.id===id))return;
  chooseServiceProfileIdentity(path,`profile:${crypto.randomUUID()}`);
}
function capturedAdoption(value:Exclude<ServiceRecoverySnapshot,{state:'idle'}>):ProfileLibraryAdoption{
  return {...value.pending.adoption,identityResolutions:value.profiles.filter(p=>!p.profile.id && value.choices[p.path]).map(p=>({path:p.path,sha256:p.sha256,profileId:value.choices[p.path]}))};
}
async function currentPlan(value:Exclude<ServiceRecoverySnapshot,{state:'idle'}>):Promise<ProfileAdoptionPlan>{
  let remote:ProfileLibraryDocument;
  try{remote=await readProfileLibrary();}
  catch(error){
    if(!(error instanceof LegacyProfilesPendingError))throw error;
    if(!sameSource(value.pending.adoption,error.adoption))throw Object.assign(new Error('The original files changed. Review the new inventory; your current choices are kept.'),{incoming:error});
    remote=error.document;
  }
  return planServiceProfileAdoption(remote,capturedAdoption(value),await readProfileLibraryAdoptions(),value.decisions);
}
export async function reviewServiceProfiles():Promise<void>{
  if(snapshot.state!=='review' || frozen || snapshot.incoming)return;
  const captured=snapshot,mine=++generation;
  publish({...captured,state:'loading',error:undefined});
  try{
    const plan=await currentPlan(captured);
    if(mine===generation)publish({...captured,state:'review',plan,error:plan.conflicts.length?plan.conflicts.join('\n'):undefined});
  }catch(error){if(mine===generation)publish({...captured,error:message(error),incoming:(error as {incoming?:LegacyProfilesPendingError}).incoming});}
}
export async function saveServiceProfiles():Promise<void>{
  if(snapshot.state!=='review' || snapshot.incoming && !frozen || !frozen && (!snapshot.plan || snapshot.plan.conflicts.length))return;
  const captured=snapshot,mine=++generation,current=frozen?.current ?? workingContext();
  publish({...captured,state:'saving',error:undefined,frozen:!!frozen});
  try{
    await requireServiceProfileAdoption();
    if(!frozen){
      const plan=await currentPlan(captured);
      if(mine!==generation)return;
      if(plan.conflicts.length){publish({...captured,plan,error:plan.conflicts.join('\n'),frozen:false});return;}
      if(!plan.alreadyAcknowledged)frozen={write:structuredClone({operationId:plan.operationId,expectedRevision:plan.document.revision,document:plan.document,adoption:plan.adoption}),current};
    }
    const request=frozen?.write;
    if(request)await persistProfileLibrary(request);
    const adoption=request?.adoption ?? captured.plan!.adoption;
    const receipt=await readProfileLibraryAdoption(adoption!.digest);
    if(!receipt || !sameSource(receipt,adoption!) || receipt.operationId!==`service-adoption:${adoption!.digest}`)throw new Error('The service has not durably acknowledged this exact source. Your choices and original files are kept; retry the same request.');
    const acknowledged=await planServiceProfileAdoption(captured.pending.document,adoption!,[receipt]);
    if(!acknowledged.alreadyAcknowledged || acknowledged.conflicts.length)throw new Error(acknowledged.conflicts.join('\n') || 'The confirmed file identity mapping could not be verified. Retry the same request.');
    const choices=Object.fromEntries((acknowledged.adoption.identityResolutions ?? []).map(c=>[c.path,c.profileId]));
    frozen=undefined;
    try{await readProfileLibrary();}
    catch(error){if(error instanceof LegacyProfilesPendingError){if(mine===generation)await beginServiceRecovery(error,true);return;}throw error;}
    if(mine!==generation)return;
    publish({...captured,state:'complete',choices,frozen:false,error:current()?undefined:'The source is recovered. Newer project or profile work was kept; reload profiles explicitly to reconcile it.'});
    if(current())await (await import('../actions')).loadCatalog();
    else setState({profileLibraryLoad:{state:'failed',error:'Service profiles were recovered. Your newer work is kept; retry loading to reconcile it.'}});
  }catch(error){
    const failure=error as {status?:number;code?:string;incoming?:LegacyProfilesPendingError};
    if([400,401,403,404,422].includes(failure.status ?? 0) || failure.status===409 && ['revision_conflict','legacy_source_changed','operation_conflict'].includes(failure.code ?? ''))frozen=undefined;
    if(mine===generation)publish({...captured,state:'review',error:message(error),frozen:!!frozen,incoming:failure.incoming});
  }finally{
    const latest=serviceRecoverySnapshot();
    if(latest.state==='saving')publish({...latest,state:'review',frozen:!!frozen});
  }
}
export async function refreshServiceRecovery():Promise<void>{
  if(snapshot.state==='idle' || snapshot.state==='saving' || snapshot.state==='loading' || frozen)return;
  const prior=snapshot;
  try{await readProfileLibrary();await (await import('../actions')).loadCatalog();publish({state:'idle'});}
  catch(error){if(error instanceof LegacyProfilesPendingError)await beginServiceRecovery(error,true);else publish({...prior,error:message(error)});}
}
export function serviceRecoveryExport():string{
  if(snapshot.state==='idle')throw new Error('No service recovery inventory is loaded.');
  return JSON.stringify({adoption:capturedAdoption(snapshot),decisions:snapshot.decisions,document:snapshot.pending.document},null,2);
}
