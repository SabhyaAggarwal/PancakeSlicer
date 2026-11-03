import { captureBrowserProfileInventory, captureRendererProfileInventory, browserProfileInventoryText } from './browser-inventory';
import { getState } from '../state/store';
export function downloadRecoverySource(name:string,text:string):void{
  const url=URL.createObjectURL(new Blob([text],{type:'application/json'}));
  const link=document.createElement('a');link.href=url;link.download=name;link.click();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
}
export async function exportBrowserRecovery():Promise<void>{
  const host=window.orcaDesktop;
  if(host){
    const source=await import('../persist/desktop-legacy');
    if(!host.legacyProjects || !host.legacyRendererStorage)throw source.desktopLegacyIncomplete();
    const capture=await source.captureDesktopRenderer(host.legacyRendererStorage);
    const origins=capture.origins.map(origin=>({origin:origin.origin,inventory:captureRendererProfileInventory(getState(),origin)}));
    await source.verifyDesktopRendererCapture(host.legacyRendererStorage,capture);
    downloadRecoverySource('desktop-profile-recovery.json',JSON.stringify({scope:'profiles-by-original-origin',origins},null,2));
    return;
  }
  downloadRecoverySource('browser-profile-recovery.json',browserProfileInventoryText(await captureBrowserProfileInventory(getState())));
}
