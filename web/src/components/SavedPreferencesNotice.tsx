import { useSyncExternalStore } from 'react';
import { initialiseSavedPreferences, savedPreferencesSnapshot, subscribeSavedPreferences } from '../saved-preferences';
export default function SavedPreferencesNotice() {
  const saved = useSyncExternalStore(subscribeSavedPreferences, savedPreferencesSnapshot);
  if (saved.state !== 'failed' && (!saved.hydrated || saved.workspaceReady)) return null;
  return <div className="saved-preferences-notice" role={saved.state==='failed'?'alert':'status'}><span>{!saved.workspaceReady?'Historical storage recovery is incomplete. Changes remain in memory.':'Preferences haven’t been saved.'} {saved.error}</span><button type="button" className="btn" onClick={() => void initialiseSavedPreferences(true).catch(() => {})}>Retry recovery</button></div>;
}
