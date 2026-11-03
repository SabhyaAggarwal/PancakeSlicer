import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { effectiveMode, getState, resolvePreset, toast, useStore } from '../state/store';
import { changedKeys, saveProfile } from './model';
import { commitProfileRecords, profileOriginIsCurrent } from './transactions';
import { findProfile, profileKey } from './identity';
import { ColorPicker } from '../components/ColorPicker';
import { profileColour,profileSummary } from './metadata';
import { MenuButton } from '../components/Menu';
import { WorksWith, applicabilityKey } from './WorksWith';
import type { ProfileApplicability } from './applicability';
import { acknowledgeProjectProfileChanges,type ProjectProfileChanges } from './project-draft';
import { requestPreset } from '../components/PresetCombo';
import { sameValue } from '../state/value-equality';
import { renameOwnedProfile } from './material-library';
import { Glyph } from '../components/Glyph';
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
export function ProfileEditor({type, name, draft, update, onBack, onSavingChange, onRemove, onDuplicate, foldActions = false, renameRequest}: {type: PresetType; name: string; draft?: ProfileDraft; renameRequest?: {key: string; at: number}; update(key: string, draft: ProfileDraft | null): void; onBack(): void; onRemove(): void; onDuplicate?(): void; onSavingChange?(saving: boolean): void; foldActions?: boolean}) {
  const users = useStore(s => s.userPresets);
  const ready=useStore(s=>s.profileLibraryLoad.state==='ready');
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
  const revert = () => {update(key, null); setError(null);};
  const save = async () => {
    if (inFlight.current || !dirty) return;
    const s = getState(), currentSource=findProfile(s,type,name);
    const submitted={...latest.current,values:structuredClone(latest.current.values),applicability:structuredClone(latest.current.applicability)};
    if (!s.api) {setError('Not connected. Your edits are kept.'); return;}
    if (!source || resolved.error || !resolved.base) {setError(resolved.error ?? 'This profile is unavailable. Your edits are kept.'); return;}
    if (!profileOriginIsCurrent(currentSource,submitted.origin)) { setError('This profile changed elsewhere. Cancel edits to load the saved version.'); return; }
    inFlight.current = true; setSaveState({value: 'saving'}); onSavingChange?.(true);
    try {
      const p = {...saveProfile(s, type, currentSource!.name, profileKey(currentSource!), submitted.values), id: profileKey(currentSource!), applicability:submitted.applicability};
      await commitProfileRecords([p],{apply:submitted.staged ? state=>acknowledgeProjectProfileChanges(state,submitted.staged!,submitted.values) : undefined});
      const saved=getState(),origin=findProfile(saved,type,profileKey(p)),acknowledged=resolvePreset(saved,type,profileKey(p));
      if(!origin || acknowledged.error || !profileOriginIsCurrent(origin,submitted.origin))throw new Error('The profile changed again while saving. Your edits are kept.');
      const next=acknowledgedProfileDraft(latest.current,submitted,origin,acknowledged.config);
      update(key,next); setSaveState({value: 'idle'}); toast(`Saved ${profileName}`);
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { inFlight.current = false; onSavingChange?.(false); }
  };
  const renamable = !!source && source.kind !== 'master';
  const [naming, setNaming] = useState<string | null>(null), [nameError, setNameError] = useState<string | null>(null);
  const nameInput = useRef<HTMLInputElement>(null);
  const startRename = () => {if (!renamable || saving) return; setNaming(profileName); setNameError(null);};
  useEffect(() => {if (renameRequest?.key === name && Date.now() - renameRequest.at < 2000) startRename();}, [renameRequest]);
  const renameTo = async (raw: string): Promise<boolean> => {
    const next = raw.trim(), s = getState(), current = findProfile(s, type, name);
    if (!current) return false;
    if (next === current.name) return true;
    if (!next) {setNameError('Enter a name'); return false;}
    if (s.userPresets.some((p) => p.type === type && !p.archived && profileKey(p) !== profileKey(current) && p.name.toLocaleLowerCase() === next.toLocaleLowerCase())) {setNameError(`“${next}” is already taken`); return false;}
    const prior = current.name;
    try {
      await commitProfileRecords([renameOwnedProfile(current, next)]);
      const renamed = findProfile(getState(), type, name);
      if (draft && renamed) update(key, {...latest.current, origin: renamed});
      toast(`Renamed to ${next}`, {label: 'Undo', run: () => void renameTo(prior)});
      return true;
    } catch (e) {setError(e instanceof Error ? e.message : String(e)); return false;}
  };
  const commitName = async () => {if (naming === null) return; const value = naming; if (await renameTo(value)) {setNaming(null); setNameError(null);}};
  if (!resolved.user || !resolved.base || resolved.error) return <section className="pf-full-editor"><div className="pf-editor-error" role="alert"><strong>{resolved.user?.name ?? 'Profile unavailable'}</strong><p>{resolved.error ?? 'This profile could not be loaded.'}</p><button type="button" className="btn" onClick={() => void (async () => {try {const {loadCatalog}=await import('../actions');await loadCatalog();const s=getState();if(s.profileLibraryLoad.state==='failed')throw new Error(s.profileLibraryLoad.error);if(!findProfile(s,type,name))throw new Error('Profile is still unavailable.');} catch(e) {setError(e instanceof Error ? e.message : String(e));}})()}>Retry</button>{error && <p>{error}</p>}</div><button type="button" className="btn-ghost" onClick={onBack}>Close profiles</button></section>;
  return <section className="pf-full-editor" aria-label={`Edit ${profileName}`}>
    <div className="pf-editor-heading">
      <div className="pf-editor-identity">{type === 'filament' && <ColorPicker value={profileColour(values) ?? ''} label="Default filament color" disabled={saving} onCommit={colour=>set('filament_colour',Array.isArray(values.filament_colour) ? [colour] : colour)}/>}{naming !== null ? <input ref={nameInput} autoFocus onFocus={e => e.currentTarget.select()} className="pf-name-input" aria-label="Profile name" aria-invalid={!!nameError} aria-describedby={nameError ? `${key}-name-error` : undefined} value={naming} autoComplete="off" spellCheck={false}
        onChange={e => {setNaming(e.target.value); setNameError(null);}} onBlur={() => void commitName()}
        onKeyDown={e => {if (e.key === 'Enter') {e.preventDefault(); void commitName();} else if (e.key === 'Escape') {e.preventDefault(); e.stopPropagation(); setNaming(null); setNameError(null);}}}/>
        : <span className="pf-editor-name"><strong onClick={renamable ? startRename : undefined} data-renamable={renamable || undefined}>{profileName}</strong>{renamable && <button type="button" className="reset-icon pf-rename" aria-label="Rename" disabled={saving} onClick={startRename}><Glyph name="edit" size={14}/></button>}</span>}
      {nameError ? <div className="pf-editor-summary pf-name-error" id={`${key}-name-error`} role="alert">{nameError}</div> : <div className="pf-editor-summary">{profileSummary(type, values)}{draft?.staged ? ' · Includes project changes' : ''}</div>}</div>
      <div className="pf-editor-controls">
        <button type="button" className={`btn pf-editor-save${dirty ? ' primary' : ''}`} disabled={saving || !dirty || !ready} onClick={() => void save()}>{saving ? 'Saving…' : 'Save'}</button>
        {dirty && !foldActions && <button type="button" className="btn" disabled={saving} onClick={revert}>Revert</button>}
        {onDuplicate && !foldActions && <button type="button" className="btn" disabled={saving || dirty || !ready} onClick={onDuplicate}>Duplicate</button>}
        <MenuButton label="Profile actions" className="icon-btn" items={[...(renamable ? [{label: 'Rename', disabled: saving, action: startRename}] : []), ...(foldActions && dirty ? [{label: 'Revert', disabled: saving, action: revert}] : []), ...(foldActions && onDuplicate ? [{label: 'Duplicate', disabled: saving || dirty || !ready, action: onDuplicate}] : []), {label: type === 'filament' ? 'Use in slot 1' : 'Use in project', disabled:saving || dirty, action:()=>requestPreset(type,0,key && profileKey(source!))}, {label:'Export',action:()=>void import('../preset-io').then(m=>m.exportPresetJson(source!))}, {label: 'Reset to base settings', disabled: saving || !resolved.base, action: () => {if(resolved.base) update(key, {...latest.current, values: resolved.base.config});}}, 'sep', {label:'Delete',disabled:saving,action:onRemove}]}>⋯</MenuButton>
      </div>
    </div>
    {error && <div className="pf-editor-error" role="alert">{error}</div>}
    <fieldset className="pf-editor-fields" disabled={saving} aria-busy={saving}><SettingsTab type={type} hidePreset editor={editor} auxiliary={type !== 'machine' && source ? {label:'Works with',icon:'param_dependencies_printers',modified:applicabilityDirty,content:<WorksWith source={source} config={values} value={applicability} onChange={next=>update(key,{...latest.current,applicability:next})}/>} : undefined}/></fieldset>
  </section>;
}
