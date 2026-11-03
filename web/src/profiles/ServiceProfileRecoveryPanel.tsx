import type { ServiceRecoverySnapshot } from './service-recovery';
import { chooseServiceProfileIdentity, createServiceProfileIdentity, keepServiceProfileCurrent, refreshServiceRecovery, reviewServiceProfiles, saveServiceProfiles, serviceRecoveryExport } from './service-recovery';
import { ServiceProfileIdentityReview } from './ServiceProfileIdentityReview';
import { downloadRecoverySource } from './recovery-export';

export function ServiceProfileRecoveryPanel({recovery,onBrowse}:{recovery:Exclude<ServiceRecoverySnapshot,{state:'idle'}>;onBrowse():void}){
  const busy=recovery.state==='loading' || recovery.state==='saving';
  const download=()=>downloadRecoverySource('service-profile-recovery.json',serviceRecoveryExport());
  return <section className="pf-service-recovery" aria-label="Recover original service profiles" aria-busy={busy}>
    <header><h2>Review your saved profiles</h2><p>Earlier versions left original profile files. Confirm how they belong in your service library to enable Add and Save. Original files, existing saved values and your current edits are kept.</p></header>
    <div className="pf-service-recovery-actions">
      {recovery.incoming?<button type="button" className="btn" disabled={busy || recovery.frozen} onClick={()=>void refreshServiceRecovery()}>Review changed inventory</button>:<button type="button" className="btn" disabled={busy || recovery.frozen} onClick={()=>void reviewServiceProfiles()}>Review recovery</button>}
      <button type="button" className="btn primary" disabled={busy || !recovery.frozen && (!!recovery.incoming || !recovery.plan || !!recovery.plan.conflicts.length)} onClick={()=>void saveServiceProfiles()}>{recovery.frozen?'Retry exact recovery request':'Save recovered profiles'}</button>
      <button type="button" className="btn-ghost" onClick={download}>Export original recovery data</button>
      <button type="button" className="btn-ghost" disabled={busy || recovery.frozen} onClick={()=>void refreshServiceRecovery()}>Refresh saved records</button>
      <button type="button" className="btn-ghost" onClick={onBrowse}>Browse templates</button>
    </div>
    <p>{recovery.profiles.length} profile records · {recovery.pending.document.profiles.length} currently saved profiles · Library revision {recovery.pending.document.revision}</p>
    {!!recovery.pending.document.profiles.length && <details className="pf-canonical-saved"><summary>View currently saved profiles</summary><ul>{recovery.pending.document.profiles.map(profile=><li key={profile.id}><strong>{profile.name}</strong><span>{profile.type==='machine'?'Printer':profile.type==='filament'?'Filament':'Print settings'} · Revision {profile.revision}</span><code>{profile.id}</code></li>)}</ul></details>}
    {recovery.error && <p role="alert" className="pf-editor-error">{recovery.error}</p>}
    {busy && <p role="status">{recovery.state==='saving'?'Saving the confirmed recovery request…':'Checking original files and saved identities…'}</p>}
    <ServiceProfileIdentityReview profiles={recovery.profiles} saved={recovery.pending.document.profiles} choices={recovery.choices} kept={recovery.decisions} onKeep={keepServiceProfileCurrent} busy={busy || recovery.frozen || !!recovery.incoming} onChoose={chooseServiceProfileIdentity} onCreate={createServiceProfileIdentity}/>
    {recovery.frozen && <p>The response was not acknowledged. Identity choices are locked; Retry sends the identical captured document, choices and operation.</p>}
    {recovery.plan && !recovery.plan.conflicts.length && <p role="status">Recovery is ready for confirmation. The service checks the latest revision and original file hashes before saving.</p>}
  </section>;
}
