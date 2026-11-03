import { getState, subscribeStore, type State } from '../state/store';
import { createMachine } from '../flow/fsm';
import { saveProfileRecords } from './transactions';
import { documentGeneration } from '../state/history';
import type { PrinterProfileLibrary } from './ownership';
export interface PrinterSetupPersistence {state:'idle'|'saving'|'error';error:string|null;recovery?:'adopt_browser_inventory'|'reload_profiles'}
interface SetupIntent {snapshot:State;baseline:PrinterProfileLibrary;isCurrent():boolean}
type SetupState='unhydrated'|'blocked'|'idle'|'saving'|'failed';
type SetupEvent={type:'HYDRATED'}|{type:'CHANGE';intent:SetupIntent}|{type:'DONE';request:number}|{type:'FAIL';request:number;error:string;changed:boolean};
interface SetupContext {request:number;desired:SetupIntent|null;writing:SetupIntent|null;error:string|null}
let watching=false,generation=0,baseline:PrinterProfileLibrary|undefined;
const mine=(c:SetupContext,e:{request:number})=>e.request===c.request;
const late={};
const hydratedEdge={target:'idle' as const,action:(c:SetupContext)=>({request:c.request+1,desired:null,writing:null,error:null})};
const change={target:'saving' as const,action:(_:SetupContext,e:{intent:SetupIntent})=>({desired:e.intent,error:null})};
const setups=createMachine<SetupState,SetupEvent,SetupContext>({
  id:'printer-setups',initial:'unhydrated',strict:true,context:{request:0,desired:null,writing:null,error:null},
  states:{
    unhydrated:{on:{HYDRATED:hydratedEdge,CHANGE:{target:'blocked',action:()=>({error:'Restore or adopt the saved printer setups before saving this working setup.'})},DONE:late,FAIL:late}},
    blocked:{on:{HYDRATED:hydratedEdge,CHANGE:{},DONE:late,FAIL:late}},
    idle:{on:{HYDRATED:hydratedEdge,CHANGE:change,DONE:late,FAIL:late}},
    saving:{
      entry:(c)=>{const intent=c.desired!,request=c.request+1;void write(intent,request);return{request,desired:null,writing:intent};},
      on:{HYDRATED:hydratedEdge,CHANGE:{action:(_,e)=>({desired:e.intent})},
        DONE:[{target:'saving',guard:(c,e)=>mine(c,e) && !!c.desired},{target:'idle',guard:mine,action:()=>({writing:null})},late],
        FAIL:[{target:'saving',guard:(c,e)=>mine(c,e) && e.changed && !!c.desired},
          {target:'failed',guard:mine,action:(c,e)=>({error:e.error,desired:c.desired ?? c.writing,writing:null})},late]}},
    failed:{on:{HYDRATED:hydratedEdge,CHANGE:change,DONE:late,FAIL:late}},
  },
});
export const printerSetupsMachine=setups;
async function write(intent:SetupIntent,request:number){
  try{await saveProfileRecords(intent.snapshot,[],`printer-setup:${crypto.randomUUID()}`,intent.snapshot.printerLibrary,undefined,intent);setups.send({type:'DONE',request});}
  catch(error){setups.send({type:'FAIL',request,error:error instanceof Error?error.message:String(error),changed:(error as {code?:string}).code==='setup_context_changed'});}
}
const view=():PrinterSetupPersistence=>{const {value,context}=setups.get();
  return value==='saving'?{state:'saving',error:null}:value==='failed'?{state:'error',error:context.error}
    :value==='blocked'?{state:'error',error:context.error,recovery:'adopt_browser_inventory'}:{state:'idle',error:null};};
let status=view();
const listeners=new Set<()=>void>();
setups.subscribe(()=>{status=view();listeners.forEach(l=>l());});
export const printerSetupPersistence=()=>status;
export const subscribePrinterSetupPersistence=(listener:()=>void)=>{listeners.add(listener);return()=>{listeners.delete(listener);};};
export function acceptHydratedPrinterSetups():void{baseline=getState().printerLibrary;generation++;setups.send({type:'HYDRATED'});watchPrinterSetups();}
function schedule(s:State){
  const at=generation,document=documentGeneration();
  setups.send({type:'CHANGE',intent:{snapshot:s,baseline:baseline!,isCurrent:()=>{const now=getState();return at===generation && document===documentGeneration() && now.api===s.api
    && now.printer===s.printer && now.process===s.process && now.printerLibrary===s.printerLibrary && now.filamentSlots===s.filamentSlots
    && now.slotColours===s.slotColours && now.filamentEdits===s.filamentEdits && now.mixes===s.mixes;}}});
}
export function watchPrinterSetups():void{
  if(watching)return;watching=true;let previous=getState();
  subscribeStore(()=>{const current=getState(),before=previous;previous=current;
    if(current.profileLibraryLoad.state==='ready' && before.profileLibraryLoad.state==='ready' && current.printerLibrary!==before.printerLibrary)schedule(current);});
}
export function flushPrinterSetups():Promise<void>{
  return new Promise((resolve,reject)=>{
    let done=false,stop:(()=>void)|undefined;
    const settle=()=>{
      const {value,context}=setups.get();
      if(done || value==='saving')return;
      done=true;stop?.();
      if(value==='failed' || value==='blocked')reject(new Error(context.error ?? 'Printer setups need recovery.'));else resolve();
    };
    settle();if(!done)stop=setups.subscribe(settle);
  });
}
