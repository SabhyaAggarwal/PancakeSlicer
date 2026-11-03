import { useCallback, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { Dropdown } from '../components/Dropdown';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { getState, markStale, resolvePreset, setState, toast, useStore } from '../state/store';
import { changedKeys, saveProfile } from './model';
import { rememberLocalProfile } from '../persist/profiles';
import { profileSummary } from './metadata';
export interface ProfileDraft {values: Config; baseline: Config; origin: UserPreset | undefined}
export function ProfileEditor({type, name, draft, update, actions}: {type: PresetType; name: string; draft?: ProfileDraft; update(key: string, draft: ProfileDraft | null): void; actions: React.ReactNode}) {
  const users = useStore(s => s.userPresets);
  const resolved = useMemo(() => resolvePreset(getState(), type, name), [users, type, name]);
  const key = JSON.stringify([type, name]);
  const baseline = draft?.baseline ?? resolved.config, values = draft?.values ?? resolved.config;
  const changes = useMemo(() => changedKeys(values, baseline), [values, baseline]);
  const [mode, setMode] = useState<'simple'|'advanced'|'develop'>('advanced');
  const [saving, setSaving] = useState(false), [error, setError] = useState<string | null>(null);
  const latest = useRef({values, baseline, origin: draft?.origin ?? resolved.user}); latest.current = {values, baseline, origin: draft?.origin ?? resolved.user};
  const set = useCallback((k: string, value: ConfigValue) => { const current = latest.current; update(key, {...current, values: {...current.values, [k]: value}}); }, [key, update]);
  const reset = useCallback((k: string) => { const current = latest.current; const next = {...current.values}; if (current.baseline[k] === undefined) delete next[k]; else next[k] = current.baseline[k]; update(key, {...current, values: next}); }, [key, update]);
  const editor: ProfileFields = useMemo(() => ({name, config: values, baseline, changes, set, reset, mode}), [name, values, baseline, changes, set, reset, mode]);
  const dirty = Object.keys(changes).length > 0;
  const save = async () => {
    if (!dirty || saving) return;
    const s = getState();
    if (!s.api || !resolved.user) return;
    if (resolvePreset(s, type, name).user !== latest.current.origin) { setError('This profile changed elsewhere. Cancel these edits to reload it.'); return; }
    setSaving(true); setError(null);
    try {
      const p = saveProfile(s, type, name, name, values);
      await s.api.saveUserPreset(p); rememberLocalProfile(p, s.api.mock);
      setState(st => ({userPresets: st.userPresets.map(u => u.type === type && u.name === name ? p : u), job: markStale(st.job)}));
      update(key, null); toast(`Saved ${name}`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setSaving(false); }
  };
  return <section className="pf-full-editor" aria-label={`Edit ${name}`}>
    <div className="pf-editor-heading"><div><strong>{name}</strong><div className="pf-editor-summary">{profileSummary(type, values)}</div></div><Dropdown aria-label="Settings visibility" value={mode} onValueChange={v => setMode(v as typeof mode)} options={[{value: 'simple',label: 'Simple'},{value: 'advanced',label: 'Advanced'},{value: 'develop',label: 'Expert'}]}/></div>
    <SettingsTab type={type} hidePreset editor={editor}/>
    {error && <div className="pf-editor-error" role="alert">{error}</div>}
    <div className="pf-actions">
      {dirty && <><button type="button" className="btn primary" disabled={saving} title={saving ? 'Waiting for profile save' : 'Save this profile'} onClick={() => void save()}>{saving ? 'Saving…' : 'Save'}</button><button type="button" className="btn-ghost" disabled={saving} title={saving ? 'Wait for the save to finish' : 'Discard edits to this profile'} onClick={() => {update(key, null); setError(null);}}>Cancel edits</button></>}
      {actions}
      <button type="button" className="btn-ghost" disabled={saving || !resolved.base} title={saving ? 'Wait for the save to finish' : !resolved.base ? 'The profile base is unavailable' : 'Reset settings in this draft; Save applies them'} onClick={() => {if(resolved.base) update(key, {...latest.current, values: resolved.base.config});}}>Reset settings</button>
    </div>
  </section>;
}
