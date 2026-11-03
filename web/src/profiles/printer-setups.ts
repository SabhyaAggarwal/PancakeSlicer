import { getState, setState, subscribeStore, type State } from '../state/store';
import { readProfileLibrary } from '../persist/profile-library';
import { saveProfileRecords } from './transactions';
import { capturePrinterMaterials, planPrinterSwitch } from './material-library';
export interface PrinterSetupPersistence {state:'idle'|'saving'|'error';error:string|null;recovery?:'adopt_browser_inventory'|'reload_profiles'}
let status:PrinterSetupPersistence={state:'idle',error:null},tail:Promise<void>=Promise.resolve(),running=false,desired:State|null=null,watching=false,generation=0,hydrated=false;
const listeners=new Set<()=>void>();
const publish=(next:PrinterSetupPersistence)=>{status=next;listeners.forEach(l=>l());};
export const printerSetupPersistence=()=>status;
export const subscribePrinterSetupPersistence=(listener:()=>void)=>{listeners.add(listener);return()=>{listeners.delete(listener);};};
export function acceptHydratedPrinterSetups():void{hydrated=true;publish({state:'idle',error:null});watchPrinterSetups();}
export async function hydratePrinterSetups(options:{restoreStartupPlaceholder?:boolean}={}):Promise<void>{
  const initial=getState(),mine=++generation;
  let library;
  try{library=await readProfileLibrary(initial.api?.mock);if(!library)throw new Error('The required profile-library API is unavailable. Keep browser edits and update the service.');}
  catch(error){if(mine===generation){hydrated=false;publish({state:'error',error:String(error),recovery:'reload_profiles'});}throw error;}
  if(mine!==generation)return;
  const s=getState();
  const changed=s.api!==initial.api || s.userPresets!==initial.userPresets || s.printer!==initial.printer || s.printerLibrary!==initial.printerLibrary || s.filamentSlots!==initial.filamentSlots || s.slotColours!==initial.slotColours || s.filamentEdits!==initial.filamentEdits || s.mixes!==initial.mixes;
  const local=capturePrinterMaterials(s),selected=library.printers.setups[s.printer];
  const differs=(id:string)=>!library.printers.setups[id] || JSON.stringify(local.setups[id])!==JSON.stringify(library.printers.setups[id]);
  const localOnly=Object.keys(local.setups).some(differs),inactiveLocal=Object.keys(local.setups).some(id=>id!==s.printer && differs(id));
  if(changed || inactiveLocal || localOnly && !options.restoreStartupPlaceholder || !library.revision && s.userPresets.length){
    hydrated=false;
    publish({state:'error',error:'Browser printer slots, colours or tuning have not been adopted into the saved library. Export or explicitly adopt them before restoring saved setups.',recovery:'adopt_browser_inventory'});
    return;
  }
  if(library.revision>0){
    const setup=planPrinterSwitch({...s,printerLibrary:library.printers},s.printer);
    setState({profileLibraryRevision:library.revision,printerLibrary:library.printers,
      ...(selected?{filamentSlots:selected.slots.map(slot=>slot.materialId),slotColours:selected.slots.map(slot=>slot.colour),filamentEdits:selected.tuning,mixes:selected.mixes}:{}),
      profileRecoveries:setup.recoveries});
  }else setState({profileLibraryRevision:0});
  hydrated=true;publish({state:'idle',error:null});
}
function schedule(s:State){
  if(!hydrated){publish({state:'error',error:'Restore or adopt the saved printer setups before saving this working setup.',recovery:'adopt_browser_inventory'});return;}
  desired=s;if(running)return;running=true;
  tail=(async()=>{
    publish({state:'saving',error:null});
    while(desired){const snapshot=desired;desired=null;try{await saveProfileRecords({...snapshot,profileLibraryRevision:getState().profileLibraryRevision},[],`printer-setup:${crypto.randomUUID()}`,snapshot.printerLibrary);}
      catch(error){desired=null;publish({state:'error',error:error instanceof Error?error.message:String(error)});throw error;}}
    publish({state:'idle',error:null});
  })().finally(()=>{running=false;});
  void tail.catch(()=>{});
}
export function watchPrinterSetups():void{
  if(watching)return;watching=true;let previous=getState();
  subscribeStore(()=>{const current=getState();if(current.printerLibrary!==previous.printerLibrary)schedule(current);previous=current;});
}
export const flushPrinterSetups=():Promise<void>=>status.state==='error'?Promise.reject(new Error(status.error ?? 'Printer setups need recovery.')):tail;
export function retryPrinterSetups():Promise<void>{schedule(getState());return tail;}
