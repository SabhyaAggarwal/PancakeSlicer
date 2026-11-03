import { captureBrowserProfileInventory, browserProfileInventoryText } from './browser-inventory';
import { getState } from '../state/store';
export function downloadRecoverySource(name:string,text:string):void{
  const url=URL.createObjectURL(new Blob([text],{type:'application/json'}));
  const link=document.createElement('a');link.href=url;link.download=name;link.click();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
}
export async function exportBrowserRecovery():Promise<void>{
  downloadRecoverySource('browser-profile-recovery.json',browserProfileInventoryText(await captureBrowserProfileInventory(getState())));
}
