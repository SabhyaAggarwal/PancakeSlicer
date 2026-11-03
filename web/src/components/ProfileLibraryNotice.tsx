import { useState } from 'react';
import { loadCatalog } from '../actions';
import { setState, useStore } from '../state/store';

export function ProfileLibraryNotice({recoveryContext=false}:{recoveryContext?:boolean}) {
  const load = useStore(s => s.profileLibraryLoad);
  const [exportError,setExportError]=useState<string|null>(null);
  const exportBrowser=async()=>{try{await(await import('../profiles/recovery-export')).exportBrowserRecovery();setExportError(null);}catch(error){setExportError(String(error));}};
  if (load.state === 'ready') return null;
  if (load.state === 'loading') return <div className="profile-library-notice" role="status">Loading saved profiles…</div>;
  const recover=load.recovery==='adopt_service_profiles';
  return <div className="profile-library-notice" role="alert">
    <strong>{recover?'Review your saved profiles':'Saved profiles need attention'}</strong><span>{recover?'Earlier versions left original profile files. Review them once to enable Add and Save. Your current edits and original files are kept.':load.error}</span>
    <div><button type="button" className="btn" onClick={()=>recover?void import('../profiles/service-recovery').then(m=>m.openSavedProfileRecovery()):void loadCatalog()}>{recover?'Review saved profiles':'Retry loading'}</button><button type="button" className="btn-ghost" onClick={() => setState({presetUx: {kind: 'presets', type: 'machine',add:true}})}>Browse templates</button></div>
    {recoveryContext && !recover && <details><summary>Original-data recovery</summary><p>Current state is saved by the service. Earlier-version sources are retained read-only for lossless recovery.</p><button type="button" className="btn" onClick={()=>void exportBrowser()}>Export original recovery data</button></details>}{exportError && <span>{exportError}</span>}
  </div>;
}
