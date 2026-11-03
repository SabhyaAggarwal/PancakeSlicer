import { useSyncExternalStore } from 'react';
import { initialiseSavedPreferences, savedPreferencesSnapshot, subscribeSavedPreferences } from '../saved-preferences';
export default function SavedPreferencesNotice() {
  const saved = useSyncExternalStore(subscribeSavedPreferences, savedPreferencesSnapshot);
  if (saved.state !== 'failed') return null;
  return <div className="saved-preferences-notice" role="alert"><span>Preferences haven’t been saved. {saved.error}</span><button type="button" className="btn" onClick={() => void initialiseSavedPreferences(true).catch(() => {})}>Retry</button></div>;
}
