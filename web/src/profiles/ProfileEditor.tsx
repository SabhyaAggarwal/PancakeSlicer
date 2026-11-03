import { useCallback, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { effectiveMode, getState, resolvePreset, toast, useStore } from '../state/store';
import { changedKeys, saveProfile } from './model';
import { commitProfileRecords, profileOriginIsCurrent } from './transactions';
import { linkedProfiles } from './library-actions';
import { findProfile, profileKey } from './identity';
import { ColorPicker } from '../components/ColorPicker';
import { profileColour,profileSummary } from './metadata';
import { MenuButton } from '../components/Menu';
import { WorksWith, applicabilityKey } from './WorksWith';
import type { ProfileApplicability } from './applicability';
import { acknowledgeProjectProfileChanges,type ProjectProfileChanges } from './project-draft';
import { requestPreset } from '../components/PresetCombo';
import { sameValue } from '../state/value-equality';
export interface ProfileDraft {values: Config; baseline: Config; origin: UserPreset | undefined; printer: string; applicability?: ProfileApplicability; baselineApplicability?: ProfileApplicability;staged?:ProjectProfileChanges}
export function acknowledgedProfileDraft(current:ProfileDraft,submitted:ProfileDraft,origin:UserPreset,baseline:Config):ProfileDraft|null {
  const keys=new Set([...Object.keys(current.values),...Object.keys(submitted.values)]);
  const unchanged=[...keys].every(key=>sameValue(key,current.values[key],submitted.values[key]));
  if(unchanged && applicabilityKey(current.applicability)===applicabilityKey(submitted.applicability) && current.staged===submitted.staged && current.printer===submitted.printer)return null;
  const values={...baseline};
  for(const key of keys)if(!sameValue(key,current.values[key],submitted.values[key])){
    if(current.values[key]===undefined)delete values[key];else values[key]=current.values[key];
  }
  return {...current,values,baseline,origin,baselineApplicability:origin.applicability};
}
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
  const dirty = Object.keys(changes).length > 0 || applicabilityDirty || !!draft?.staged;
  const links=source?.kind === 'master' ? linkedProfiles(getState(),type,profileKey(source)) : [];
  const save = async () => {
    if (inFlight.current || !dirty) return;
    const s = getState(), currentSource=findProfile(s,type,name);
    const submitted={...latest.current,values:structuredClone(latest.current.values),applicability:structuredClone(latest.current.applicability)};
    if (!s.api) {setError('The profile service is unavailable. Your edits have been kept. Reconnect and try Save again.'); return;}
    if (!source || resolved.error || !resolved.base) {setError(resolved.error ?? 'This profile is unavailable. Your edits have been kept. Retry or repair the profile before saving.'); return;}
    if (!profileOriginIsCurrent(currentSource,submitted.origin)) { setError('This profile changed elsewhere. Your edits have been kept. Cancel edits to reload the saved version.'); return; }
    inFlight.current = true; setSaveState({value: 'saving'}); onSavingChange?.(true);
    try {
      const p = {...saveProfile(s, type, currentSource!.name, profileKey(currentSource!), submitted.values), id: profileKey(currentSource!), applicability:submitted.applicability};
      await commitProfileRecords([p],{apply:submitted.staged ? state=>acknowledgeProjectProfileChanges(state,submitted.staged!,submitted.values) : undefined});
      const saved=getState(),origin=findProfile(saved,type,profileKey(p)),acknowledged=resolvePreset(saved,type,profileKey(p));
      if(!origin || acknowledged.error || !profileOriginIsCurrent(origin,submitted.origin))throw new Error('The saved profile changed again. Your draft has been kept for comparison.');
      const next=acknowledgedProfileDraft(latest.current,submitted,origin,acknowledged.config);
      update(key,next); setSaveState({value: 'idle'}); toast(next ? `Saved ${profileName}; newer edits kept` : `Saved ${profileName}`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { inFlight.current = false; onSavingChange?.(false); }
  };
  if (!resolved.user || !resolved.base || resolved.error) return <section className="pf-full-editor"><div className="pf-editor-error" role="alert"><strong>{resolved.user?.name ?? 'Profile unavailable'}</strong><p>{resolved.error ?? 'The selected profile could not be resolved. Its draft has been kept.'}</p><button type="button" className="btn" onClick={() => void (async () => {try {const {loadCatalog}=await import('../actions');await loadCatalog();const s=getState();if(s.profileLibraryLoad.state==='failed')throw new Error(s.profileLibraryLoad.error);if(!findProfile(s,type,name))throw new Error('Profile is still unavailable. Import its profile file to repair it.');} catch(e) {setError(e instanceof Error ? e.message : String(e));}})()}>Retry</button>{error && <p>{error}</p>}</div><button type="button" className="btn-ghost" onClick={onBack}>Close profiles</button></section>;
  return <section className="pf-full-editor" aria-label={`Edit ${profileName}`}>
    <div className="pf-editor-heading">
      <div className="pf-editor-identity">{type === 'filament' && <ColorPicker value={profileColour(values) ?? ''} label="Default filament color" disabled={saving} onCommit={colour=>set('filament_colour',Array.isArray(values.filament_colour) ? [colour] : colour)}/>}<strong>{profileName}</strong><div className="pf-editor-summary">{profileSummary(type, values)}</div>{draft?.staged && <div className="pf-editor-summary">Reviewing project changes · Save updates this reusable profile</div>}{links.length > 0 && <div className="pf-editor-summary">Linked profiles: {links.map(p=>p.name).join(', ')}</div>}</div>
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
