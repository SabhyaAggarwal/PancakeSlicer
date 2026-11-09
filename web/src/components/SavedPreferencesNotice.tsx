import { useSyncExternalStore } from 'react';
import { initialiseSavedPreferences, savedPreferencesSnapshot, subscribeSavedPreferences } from '../saved-preferences';
export default function SavedPreferencesNotice() {
  const saved = useSyncExternalStore(subscribeSavedPreferences, savedPreferencesSnapshot);
  if (saved.state !== 'failed') return null;
  return <div className="saved-preferences-notice" role="alert"><span>Oops! Couldn't save preferences.</span><button type="button" className="btn" onClick={() => void initialiseSavedPreferences(true).catch(() => {})}>Retry</button></div>;
}
