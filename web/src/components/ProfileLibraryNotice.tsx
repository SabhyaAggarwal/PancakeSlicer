import { useState } from 'react';
import { loadCatalog } from '../actions';
import { setState, useStore } from '../state/store';

export function ProfileLibraryNotice() {
  const load = useStore(s => s.profileLibraryLoad);
  const [exportError,setExportError]=useState<string|null>(null);
  const exportBrowser=async()=>{try{await(await import('../profiles/recovery-export')).exportBrowserRecovery();setExportError(null);}catch(error){setExportError(String(error));}};
  if (load.state === 'ready') return null;
  if (load.state === 'loading') return <div className="profile-library-notice" role="status">Loading saved profiles…</div>;
  const recover=load.recovery==='adopt_service_profiles';
  return <div className="profile-library-notice" role="alert">
    <strong>Profile library needs attention</strong><span>{load.error}</span>
    <div><button type="button" className="btn" onClick={()=>recover?setState({presetUx:{kind:'presets',type:'machine'}}):void loadCatalog()}>{recover?'Review saved profiles':'Retry'}</button><button type="button" className="btn-ghost" onClick={() => setState({presetUx: {kind: 'presets', type: 'machine',add:true}})}>Browse templates</button><button type="button" className="btn-ghost" onClick={()=>void exportBrowser()}>Export browser data</button></div>{exportError && <span>{exportError}</span>}
  </div>;
}
