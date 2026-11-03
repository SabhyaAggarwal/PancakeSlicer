import { useCallback, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { Dropdown } from '../components/Dropdown';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { getState, markStale, resolvePreset, setState, toast, useStore } from '../state/store';
import { changedKeys, saveProfile } from './model';
import { rememberProfileUpdate } from '../persist/profiles';
import { findProfile, matchesProfile, profileKey } from './identity';
import { profileSummary } from './metadata';
export interface ProfileDraft {values: Config; baseline: Config; origin: UserPreset | undefined; printer: string}
export function ProfileEditor({type, name, draft, update, actions}: {type: PresetType; name: string; draft?: ProfileDraft; update(key: string, draft: ProfileDraft | null): void; actions: React.ReactNode}) {
  const users = useStore(s => s.userPresets);
  const machines = useStore(s => s.machines), filaments = useStore(s => s.filaments), processes = useStore(s => s.processes);
  const ownerPrinter = useRef(draft?.printer ?? getState().printer).current;
  const resolved = useMemo(() => resolvePreset(getState(), type, name), [users, machines, filaments, processes, type, name]);
  const source = findProfile({userPresets: users}, type, name);
  const key = JSON.stringify([type, resolved.user ? profileKey(resolved.user) : name]);
  const profileName = resolved.user?.name ?? resolved.base?.name ?? 'Unavailable profile';
  const baseline = draft?.baseline ?? resolved.config, values = draft?.values ?? resolved.config;
  const changes = useMemo(() => changedKeys(values, baseline), [values, baseline]);
  const [mode, setMode] = useState<'simple'|'advanced'|'develop'>('simple');
  const [saveState, setSaveState] = useState<{value: 'idle' | 'saving'} | {value: 'failed'; message: string}>({value: 'idle'});
  const saving = saveState.value === 'saving', error = saveState.value === 'failed' ? saveState.message : null;
  const setError = (message: string | null) => setSaveState(message ? {value: 'failed', message} : {value: 'idle'});
  const inFlight = useRef(false);
  const latest = useRef({values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter}); latest.current = {values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter};
  const set = useCallback((k: string, value: ConfigValue) => { const current = latest.current; update(key, {...current, values: {...current.values, [k]: value}}); }, [key, update]);
  const reset = useCallback((k: string) => { const current = latest.current; const next = {...current.values}; if (current.baseline[k] === undefined) delete next[k]; else next[k] = current.baseline[k]; update(key, {...current, values: next}); }, [key, update]);
  const editor: ProfileFields = useMemo(() => ({name, config: values, baseline, changes, set, reset, mode}), [name, values, baseline, changes, set, reset, mode]);
  const dirty = Object.keys(changes).length > 0;
  const save = async () => {
    if (inFlight.current || !Object.keys(changedKeys(latest.current.values, latest.current.baseline)).length) return;
    const s = getState();
    if (!s.api) {setError('The profile service is unavailable. Your edits have been kept. Reconnect and try Save again.'); return;}
    if (!source || resolved.error || !resolved.base) {setError(resolved.error ?? 'This profile is unavailable. Your edits have been kept. Retry or repair the profile before saving.'); return;}
    const originalPrinter = findProfile(s, 'machine', latest.current.printer);
    if (type === 'filament' && (originalPrinter ? !matchesProfile(originalPrinter, s.printer) : s.printer !== latest.current.printer)) {setError('Printer changed. Switch back to the original printer to save these filament settings.'); return;}
    if (findProfile(s, type, name) !== latest.current.origin) { setError('This profile changed elsewhere. Your edits have been kept. Cancel edits to reload the saved version.'); return; }
    inFlight.current = true; setSaveState({value: 'saving'});
    try {
      const p = {...saveProfile(s, type, source.name, source.name, latest.current.values), id: profileKey(source)};
      await s.api.saveUserPreset(p); rememberProfileUpdate(p, s.api.mock);
      setState(st => ({userPresets: st.userPresets.map(u => u.type === type && profileKey(u) === p.id ? p : u), job: markStale(st.job)}));
      update(key, null); setSaveState({value: 'idle'}); toast(`Saved ${profileName}`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { inFlight.current = false; }
  };
  if (!resolved.user || !resolved.base || resolved.error) return <section className="pf-full-editor"><div className="pf-editor-error" role="alert"><strong>{resolved.user?.name ?? 'Profile unavailable'}</strong><p>{resolved.error ?? 'The selected profile could not be resolved. Its draft has been kept.'}</p><button type="button" className="btn" onClick={() => void (async () => {try {const s = getState(); const rows = await s.api!.userPresets(); const fresh = findProfile({userPresets: rows}, type, name); if(!fresh) throw new Error('Profile is still unavailable. Import its profile file to repair it.'); setState(st => ({userPresets: [...st.userPresets.filter(p => p.type !== type || profileKey(p) !== profileKey(fresh)), fresh]}));} catch(e) {setError(e instanceof Error ? e.message : String(e));}})()}>Retry</button>{error && <p>{error}</p>}</div><div className="pf-actions">{actions}</div></section>;
  return <section className="pf-full-editor" aria-label={`Edit ${profileName}`}>
    <div className="pf-editor-heading"><div><strong>{profileName}</strong><div className="pf-editor-summary">{profileSummary(type, values)}</div></div><Dropdown aria-label="Settings visibility" value={mode} onValueChange={v => setMode(v as typeof mode)} options={[{value: 'simple',label: 'Simple'},{value: 'advanced',label: 'Advanced'},{value: 'develop',label: 'Expert'}]}/></div>
    <SettingsTab type={type} hidePreset editor={editor}/>
    {error && <div className="pf-editor-error" role="alert">{error}</div>}
    <div className="pf-actions">
      <button type="button" className="btn primary" disabled={saving || !dirty} title={saving ? 'Waiting for profile save' : !dirty ? 'No unsaved profile changes' : 'Save changes to this profile'} onClick={() => void save()}>{saving ? 'Saving…' : 'Save profile'}</button><button type="button" className="btn-ghost" disabled={saving || !dirty} title={saving ? 'Wait for the save to finish' : !dirty ? 'No unsaved profile changes' : 'Discard edits to this profile'} onClick={() => {update(key, null); setError(null);}}>Cancel edits</button>
      {actions}
      <button type="button" className="btn-ghost" disabled={saving || !resolved.base} title={saving ? 'Wait for the save to finish' : !resolved.base ? 'The profile base is unavailable' : 'Reset settings in this draft; Save applies them'} onClick={() => {if(resolved.base) update(key, {...latest.current, values: resolved.base.config});}}>Reset settings</button>
    </div>
  </section>;
}
