import { useSyncExternalStore } from 'react';
import { loadCatalog } from '../actions';
import { useStore } from '../state/store';
import { confirmDialog } from './Ask';
import { resetProfiles, saveRecoveredProfiles, serviceRecoverySnapshot, subscribeServiceRecovery } from '../profiles/service-recovery';

export async function confirmProfileReset() {
  if (await confirmDialog('Reset profiles', 'Custom profiles and printer setups will be removed. Projects are kept.', ['Reset', 'Cancel'], true)) await resetProfiles();
}

export function ProfileLibraryNotice() {
  const load = useStore(s => s.profileLibraryLoad);
  const recovery = useSyncExternalStore(subscribeServiceRecovery, serviceRecoverySnapshot, serviceRecoverySnapshot);
  if (load.state === 'failed') return <div className="profile-library-notice" role="alert">
    <div><strong>Profiles didn't load</strong><button type="button" className="btn" onClick={() => void loadCatalog()}>Retry</button></div>
    <span>{load.error}</span>
  </div>;
  if (recovery.state === 'idle') return null;
  const busy = recovery.state === 'saving' || recovery.state === 'resetting';
  const found = `${recovery.count} profile${recovery.count === 1 ? '' : 's'} found`;
  const unreadable = recovery.state === 'available' && recovery.unreadable ? ` · ${recovery.unreadable} unreadable` : '';
  const label = recovery.state === 'saving' ? 'Saving profiles…' : recovery.state === 'resetting' ? 'Resetting profiles…'
    : recovery.count || recovery.state !== 'failed' ? found + unreadable : `${recovery.action === 'reset' ? 'Reset' : 'Save'} failed`;
  return <div className="profile-library-notice" role={recovery.state === 'failed' ? 'alert' : 'status'} aria-busy={busy}>
    <div>
      <strong>{label}</strong>
      {recovery.count > 0 ? <>
        <button type="button" className="btn primary" disabled={busy} onClick={() => void saveRecoveredProfiles()}>Save profiles</button>
        <button type="button" className="btn" disabled={busy} onClick={() => void confirmProfileReset()}>Reset and start fresh</button>
      </> : recovery.state === 'failed' && <button type="button" className="btn" onClick={() => void (recovery.action === 'reset' ? resetProfiles() : saveRecoveredProfiles())}>Retry</button>}
    </div>
    {recovery.state === 'failed' && <span>{recovery.error}</span>}
  </div>;
}
