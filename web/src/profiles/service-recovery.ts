import { getState, markStale, setState } from '../state/store';
import { documentGeneration } from '../state/history';
import { commitProfileRecovery, readProfileLibrary, readProfileRecovery, type ProfileRecoveryWrite } from '../persist/profile-library';
import { normalizeUserPreset } from './model';

export type ProfileRecovery =
  | {state:'idle'}
  | {state:'available'; count:number; unreadable?:number}
  | {state:'saving'|'resetting'; count:number}
  | {state:'failed'; count:number; action:'save'|'reset'; error:string};
let snapshot:ProfileRecovery={state:'idle'},frozen:ProfileRecoveryWrite|undefined,counting=0;
const listeners=new Set<()=>void>();
export const serviceRecoverySnapshot=()=>snapshot;
export const subscribeServiceRecovery=(listener:()=>void)=>{listeners.add(listener);return()=>{listeners.delete(listener);};};
const publish=(value:ProfileRecovery)=>{snapshot=value;listeners.forEach(fn=>fn());};
const busy=()=>snapshot.state==='saving' || snapshot.state==='resetting';
const count=()=>snapshot.state==='idle'?0:snapshot.count;
const message=(error:unknown)=>error instanceof Error?error.message:String(error);

export async function refreshProfileRecovery():Promise<void>{
  if(busy() || frozen)return;
  const mine=++counting;
  try{
    const {count,unreadable}=await readProfileRecovery();
    if(mine!==counting || busy() || frozen)return;
    publish(count || unreadable?{state:'available',count,...(unreadable?{unreadable}:{})}:{state:'idle'});
  }catch{ }
}
const rejected=(error:unknown)=>{
  const failure=error as {status?:number};
  return [400,401,403,404,409,422].includes(failure.status ?? 0);
};
async function settle(write:ProfileRecoveryWrite){
  let ack;
  try{ack=await commitProfileRecovery(write);}
  catch(error){if(rejected(error) && frozen===write)frozen=undefined;throw error;}
  const document=await readProfileLibrary();
  if(document.revision<ack.revision)throw new Error('Profiles are not updated yet. Try again.');
  if(frozen===write)frozen=undefined;
  return document;
}
async function run(action:'save'|'reset'):Promise<void>{
  if(busy())return;
  const shown=count();counting++;
  publish({state:action==='save'?'saving':'resetting',count:shown});
  const api=getState().api,generation=documentGeneration(),before=getState();
  try{
    if(frozen && frozen.action!==action)await settle(frozen);
    frozen??={operationId:`profile-${action}:${crypto.randomUUID()}`,expectedRevision:(await readProfileLibrary()).revision,action};
    const document=await settle(frozen);
    if(getState().api!==api)return publish({state:'idle'});
    setState(s=>{
      const untouched=documentGeneration()===generation && !s.profileSnapshots && s.printer===before.printer && s.process===before.process
        && s.filamentSlots===before.filamentSlots && s.printerLibrary===before.printerLibrary && s.dirty===before.dirty && s.filamentEdits===before.filamentEdits
        && s.slotColours===before.slotColours && s.mixes===before.mixes && s.project===before.project && s.projectPresets===before.projectPresets
        && s.projectProfileContexts===before.projectProfileContexts && s.profileSnapshots===before.profileSnapshots;
      const selected=document.profiles.some(p=>p.id===s.printer);
      return {userPresets:document.profiles.map(normalizeUserPreset),printerLibrary:document.printers,profileLibraryRevision:document.revision,job:markStale(s.job),
        ...(action==='reset' && untouched && !selected?{printer:'',process:'',filamentSlots:[],slotColours:[],filamentEdits:{},mixes:{},dirty:{machine:{},process:{}},profileRecoveries:[]}:{})};
    });
    publish({state:'idle'});
    await (await import('./catalog-startup')).loadCatalog();
  }catch(error){
    publish({state:'failed',count:shown,action,error:message(error)});
  }
}
export const saveRecoveredProfiles=():Promise<void>=>run('save');
export const resetProfiles=():Promise<void>=>run('reset');
