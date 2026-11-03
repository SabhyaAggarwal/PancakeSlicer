import { useCallback, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { effectiveMode, getState, resolvePreset, setState, toast, useStore } from '../state/store';
import { changedKeys, saveProfile } from './model';
import { commitProfileRecords } from './transactions';
import { linkedProfiles } from './library-actions';
import { findProfile, profileKey } from './identity';
import { profileSummary } from './metadata';
import { MenuButton } from '../components/Menu';
import { WorksWith, applicabilityKey } from './WorksWith';
import type { ProfileApplicability } from './applicability';
import { acknowledgeProjectProfileChanges,type ProjectProfileChanges } from './project-draft';
import { requestPreset } from '../components/PresetCombo';
export interface ProfileDraft {values: Config; baseline: Config; origin: UserPreset | undefined; printer: string; applicability?: ProfileApplicability; baselineApplicability?: ProfileApplicability;staged?:ProjectProfileChanges}
export function ProfileEditor({type, name, draft, update, onBack, onSavingChange, onRemove}: {type: PresetType; name: string; draft?: ProfileDraft; update(key: string, draft: ProfileDraft | null): void; onBack(): void; onRemove(): void; onSavingChange?(saving: boolean): void}) {
  const users = useStore(s => s.userPresets);
  const machines = useStore(s => s.machines), filaments = useStore(s => s.filaments), processes = useStore(s => s.processes);
  const ownerPrinter = useRef(draft?.printer ?? findProfile({userPresets: users}, type, name)?.printerId ?? getState().printer).current;
  const resolved = useMemo(() => resolvePreset(getState(), type, name), [users, machines, filaments, processes, type, name]);
  const source = findProfile({userPresets: users}, type, name);
  const key = JSON.stringify([type, resolved.user ? profileKey(resolved.user) : name]);
  const profileName = resolved.user?.name ?? resolved.base?.name ?? 'Unavailable profile';
  const baseline = draft?.baseline ?? resolved.config, values = draft?.values ?? resolved.config;
  const applicability = draft ? draft.applicability : source?.applicability, baselineApplicability = draft ? draft.baselineApplicability : source?.applicability;
  const applicabilityDirty = applicabilityKey(applicability) !== applicabilityKey(baselineApplicability);
  const changes = useMemo(() => changedKeys(values, baseline), [values, baseline]);
  const mode=useStore(effectiveMode);
  const [saveState, setSaveState] = useState<{value: 'idle' | 'saving'} | {value: 'failed'; message: string}>({value: 'idle'});
  const saving = saveState.value === 'saving', error = saveState.value === 'failed' ? saveState.message : null;
  const setError = (message: string | null) => setSaveState(message ? {value: 'failed', message} : {value: 'idle'});
  const inFlight = useRef(false);
  const latest = useRef({values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter, applicability, baselineApplicability,staged:draft?.staged}); latest.current = {values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter, applicability, baselineApplicability,staged:draft?.staged};
  const set = useCallback((k: string, value: ConfigValue) => { const current = latest.current; update(key, {...current, values: {...current.values, [k]: value}}); }, [key, update]);
  const reset = useCallback((k: string) => { const current = latest.current; const next = {...current.values}; if (current.baseline[k] === undefined) delete next[k]; else next[k] = current.baseline[k]; update(key, {...current, values: next}); }, [key, update]);
  const editor: ProfileFields = useMemo(() => ({name, config: values, baseline, changes, set, reset, mode}), [name, values, baseline, changes, set, reset, mode]);
  const dirty = Object.keys(changes).length > 0 || applicabilityDirty;
  const links=source?.kind === 'master' ? linkedProfiles(getState(),type,profileKey(source)) : [];
  const save = async () => {
    if (inFlight.current || !dirty) return;
    const s = getState();
    if (!s.api) {setError('The profile service is unavailable. Your edits have been kept. Reconnect and try Save again.'); return;}
    if (!source || resolved.error || !resolved.base) {setError(resolved.error ?? 'This profile is unavailable. Your edits have been kept. Retry or repair the profile before saving.'); return;}
    if (findProfile(s, type, name) !== latest.current.origin) { setError('This profile changed elsewhere. Your edits have been kept. Cancel edits to reload the saved version.'); return; }
    inFlight.current = true; setSaveState({value: 'saving'}); onSavingChange?.(true);
    try {
      const p = {...saveProfile(s, type, source.name, profileKey(source), latest.current.values), id: profileKey(source), applicability:latest.current.applicability};
      await commitProfileRecords([p],{apply:draft?.staged ? state=>acknowledgeProjectProfileChanges(state,draft.staged!,latest.current.values) : undefined});
      update(key, null); setSaveState({value: 'idle'}); toast(`Saved ${profileName}`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { inFlight.current = false; onSavingChange?.(false); }
  };
  if (!resolved.user || !resolved.base || resolved.error) return <section className="pf-full-editor"><div className="pf-editor-error" role="alert"><strong>{resolved.user?.name ?? 'Profile unavailable'}</strong><p>{resolved.error ?? 'The selected profile could not be resolved. Its draft has been kept.'}</p><button type="button" className="btn" onClick={() => void (async () => {try {const s = getState(); const rows = await s.api!.userPresets(); const fresh = findProfile({userPresets: rows}, type, name); if(!fresh) throw new Error('Profile is still unavailable. Import its profile file to repair it.'); setState(st => ({userPresets: [...st.userPresets.filter(p => p.type !== type || profileKey(p) !== profileKey(fresh)), fresh]}));} catch(e) {setError(e instanceof Error ? e.message : String(e));}})()}>Retry</button>{error && <p>{error}</p>}</div><button type="button" className="btn-ghost" onClick={onBack}>Close profiles</button></section>;
  return <section className="pf-full-editor" aria-label={`Edit ${profileName}`}>
    <div className="pf-editor-heading">
      <div className="pf-editor-identity"><strong>{profileName}</strong><div className="pf-editor-summary">{profileSummary(type, values)}</div>{draft?.staged && <div className="pf-editor-summary">Reviewing project changes · Save updates this reusable profile</div>}{links.length > 0 && <div className="pf-editor-summary">Linked profiles: {links.map(p=>p.name).join(', ')}</div>}</div>
      <div className="pf-editor-controls">
        <button type="button" className="btn primary pf-editor-save" disabled={saving || !dirty} title={saving ? 'Waiting for profile save' : !dirty ? 'No unsaved profile changes' : 'Save changes to this profile'} onClick={() => void save()}>{saving ? 'Saving…' : 'Save profile'}</button>
        <button type="button" className="btn" disabled={saving || !dirty} title={saving ? 'Wait for the save to finish' : !dirty ? 'No unsaved profile changes' : 'Discard edits to this profile'} onClick={() => {update(key, null); setError(null);}}>Cancel edits</button>
        <button type="button" className="btn-ghost pf-delete" disabled={saving} title="Remove from the library; Undo keeps its identity and settings" onClick={onRemove}>Delete</button>
        <MenuButton label="Profile actions" className="btn-ghost" items={[{label: type === 'filament' ? 'Use in slot 1' : 'Use in project', disabled:saving || dirty, tip:dirty ? 'Save or cancel profile edits before using it' : 'Select this saved profile', action:()=>requestPreset(type,0,key && profileKey(source!))}, {label:'Export saved profile',action:()=>void import('../preset-io').then(m=>m.exportPresetJson(source!))}, {label: 'Reset settings', disabled: saving || !resolved.base, tip: 'Reset this draft to its base settings; Save applies the change', action: () => {if(resolved.base) update(key, {...latest.current, values: resolved.base.config});}}]}>More</MenuButton>
      </div>
    </div>
    {error && <div className="pf-editor-error" role="alert">{error}</div>}
    <fieldset className="pf-editor-fields" disabled={saving} aria-busy={saving}><SettingsTab type={type} hidePreset editor={editor} auxiliary={type !== 'machine' && source ? {label:'Works with',modified:applicabilityDirty,content:<WorksWith source={source} config={values} value={applicability} onChange={next=>update(key,{...latest.current,applicability:next})}/>} : undefined}/></fieldset>
  </section>;
}
