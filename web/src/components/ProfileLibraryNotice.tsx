import { useState } from 'react';
import { loadCatalog } from '../actions';
import { setState, useStore } from '../state/store';

export function ProfileLibraryNotice() {
  const load = useStore(s => s.profileLibraryLoad);
  const [exportError,setExportError]=useState<string|null>(null);
  const exportBrowser=async()=>{try{const m=await import('../profiles/browser-inventory');const inventory=await m.captureBrowserProfileInventory((await import('../state/store')).getState());const url=URL.createObjectURL(new Blob([m.browserProfileInventoryText(inventory)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='browser-profile-recovery.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);setExportError(null);}catch(error){setExportError(String(error));}};
  if (load.state === 'ready') return null;
  if (load.state === 'loading') return <div className="profile-library-notice" role="status">Loading saved profiles…</div>;
  return <div className="profile-library-notice" role="alert">
    <strong>Profile library needs attention</strong><span>{load.error}</span>
    <div><button type="button" className="btn" onClick={() => void loadCatalog()}>Retry</button><button type="button" className="btn-ghost" onClick={() => setState({presetUx: {kind: 'presets', type: 'machine',add:true}})}>Browse templates</button><button type="button" className="btn-ghost" onClick={()=>void exportBrowser()}>Export browser data</button></div>{exportError && <span>{exportError}</span>}
  </div>;
}
