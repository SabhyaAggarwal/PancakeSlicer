import { useSyncExternalStore } from 'react';
import { loadCatalog } from '../actions';
import { useStore } from '../state/store';
import { confirmDialog } from './Ask';
import { resetProfiles, saveRecoveredProfiles, serviceRecoverySnapshot, subscribeServiceRecovery } from '../profiles/service-recovery';

export function ProfileLibraryNotice() {
  const load = useStore(s => s.profileLibraryLoad);
  const recovery = useSyncExternalStore(subscribeServiceRecovery, serviceRecoverySnapshot, serviceRecoverySnapshot);
  if (load.state === 'failed') return <div className="profile-library-notice" role="alert">
    <div><strong>Profiles didn't load</strong><button type="button" className="btn" onClick={() => void loadCatalog()}>Retry</button></div>
  </div>;
  if (recovery.state === 'idle') return null;
  const busy = recovery.state === 'saving' || recovery.state === 'resetting';
  const reset = async () => {
    if (await confirmDialog('Reset profiles', 'Custom profiles and printer setups will be removed. Projects are kept.', ['Reset', 'Cancel'], true)) await resetProfiles();
  };
  const found = `${recovery.count} profile${recovery.count === 1 ? '' : 's'} found`;
  const unreadable = recovery.state === 'available' && recovery.unreadable ? ` · ${recovery.unreadable} unreadable` : '';
  return <div className="profile-library-notice" role={recovery.state === 'failed' ? 'alert' : 'status'} aria-busy={busy}>
    <div>
      <strong>{recovery.state === 'saving' ? 'Saving profiles…' : recovery.state === 'resetting' ? 'Resetting profiles…' : found + unreadable}</strong>
      <button type="button" className="btn primary" disabled={busy} onClick={() => void saveRecoveredProfiles()}>Save profiles</button>
      <button type="button" className="btn" disabled={busy} onClick={() => void reset()}>Reset and start fresh</button>
    </div>
    {recovery.state === 'failed' && <span>{recovery.error}</span>}
  </div>;
}
