import { lazy, Suspense, useCallback, useEffect, useMemo, useRef, type ReactNode } from 'react';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { effectiveMode, getState, resolvePreset, toast, useStore } from '../state/store';
import { changedKeys, saveProfile } from './model';
import { commitProfileRecords, profileOriginIsCurrent } from './transactions';
import { findProfile, profileKey } from './identity';
import { ColorPicker } from '../components/ColorPicker';
import { profileColour,profileSummary } from './metadata';
import { MenuButton } from '../components/Menu';
import { WorksWith, applicabilityKey, worksWithNone } from './WorksWith';
import type { ProfileApplicability } from './applicability';
import { acknowledgeProjectProfileChanges,type ProjectProfileChanges } from './project-draft';
import { requestPreset } from '../components/PresetCombo';
import { sameValue } from '../state/value-equality';
import { renameOwnedProfile } from './material-library';
import { Glyph } from '../components/Glyph';
import { useMachine } from '../flow/fsm';
import { createProfileSave } from '../flow/profile-save';
import { resetSettingToBase, settingSources } from './bases';
import { baseMenuItems } from './bases-ui';
import { openSplit } from './dynamic-conversions';
import { chainIsDynamic } from './chain-dynamic';
import { Choice } from '../components/kit';
import { OPTIONS } from '../options';
import { dynamicContext, scopedSave } from './save-to-profile';
import { dynamicConflicts, withLayerValue, type DynamicContext, type DynamicProfile, type DynamicRecord, type DynamicScope, type DynamicWhen } from './dynamic';
const ConnectionEditor = lazy(() => import('../printers/ConnectionEditor').then(m => ({default: m.ConnectionEditor})));
export interface ProfileDraft { applyTo?: DynamicScope; values: Config; baseline: Config; origin: UserPreset | undefined; printer: string; applicability?: ProfileApplicability; baselineApplicability?: ProfileApplicability;staged?:ProjectProfileChanges}
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
  const connections=useStore(s=>s.printerConnections.enabled);
  const m = useMemo(createProfileSave, []), snap = useMachine(m);
  const saving = snap.value === 'saving' || snap.value === 'renaming', error = snap.value === 'failed' ? snap.context.error : null;
  const setError = (message: string | null) => m.send(message ? {type: 'REJECT', error: message} : {type: 'DISMISS'});
  useEffect(() => {onSavingChange?.(saving);}, [saving]);
  useEffect(() => () => {if (m.get().value === 'saving' || m.get().value === 'renaming') onSavingChange?.(false);}, []);
  const latest = useRef<ProfileDraft>({values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter, applicability, baselineApplicability,staged:draft?.staged}); latest.current = {values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter, applicability, baselineApplicability,staged:draft?.staged,applyTo:draft?.applyTo};
  const set = useCallback((k: string, value: ConfigValue) => { const current = latest.current; update(key, {...current, values: {...current.values, [k]: value}}); }, [key, update]);
  const reset = useCallback((k: string) => { const current = latest.current; const next = {...current.values}; if (current.baseline[k] === undefined) delete next[k]; else next[k] = current.baseline[k]; update(key, {...current, values: next}); }, [key, update]);
  const resetToBase = useCallback((k: string) => void resetSettingToBase(type, name, k).catch((e) => toast(e instanceof Error ? e.message : String(e))), [type, name]);
  const editor: ProfileFields = useMemo(() => ({name, config: values, baseline, changes, set, reset, mode, sources: settingSources(getState(), type, name), resetToBase}), [name, values, baseline, changes, set, reset, mode, users, type, resetToBase]);
  const dirty = Object.keys(changes).length > 0 || applicabilityDirty || !!draft?.staged;
  const revert = () => {update(key, null); setError(null);};
  const noPrinter = applicabilityDirty && worksWithNone(type, applicability);
  const openOnWorksWith = useRef(!!source && worksWithNone(type, source.applicability)).current;
  const printer = useStore((st) => st.printer), variant = useStore((st) => st.processVariant);
  const dyn = (source as UserPreset & DynamicRecord | undefined)?.dynamic ? source as UserPreset & DynamicRecord & {dynamic: DynamicProfile} : undefined;
  const dctx = useMemo(() => dyn && dynamicContext(dyn, variant), [dyn, printer, variant, machines]);
  const conflicts = useMemo(() => dyn && dctx ? dynamicConflicts(dyn, dctx) : [], [dyn, dctx]);
  const scopes: DynamicScope[] = !dctx ? [] : type === 'process' ? ['all', 'nozzle', 'layer', 'combo'] : ['all', 'nozzle'];
  const resolveConflict = (setting: string, when: DynamicWhen, value: ConfigValue) => {
    if (!dyn || !m.can({type: 'SAVE'})) return;
    m.send({type: 'SAVE'}); const request = m.get().context.request;
    commitProfileRecords([{...dyn, id: profileKey(dyn), dynamic: withLayerValue(dyn.dynamic, when, setting, value)} as UserPreset])
      .then(() => m.send({type: 'SAVED', request}), (e) => m.send({type: 'FAIL', request, error: e instanceof Error ? e.message : String(e)}));
  };
  const save = async () => {
    if (!m.can({type: 'SAVE'}) || !dirty) return;
    if (noPrinter) {setError('Works with matches no printer.'); return;}
    const s = getState(), currentSource=findProfile(s,type,name);
    const submitted={...latest.current,values:structuredClone(latest.current.values),applicability:structuredClone(latest.current.applicability)};
    if (!s.api) {setError('Not connected. Your edits are kept.'); return;}
    if (!source || resolved.error || !resolved.base) {setError(resolved.error ?? 'This profile is unavailable. Your edits are kept.'); return;}
    if (!profileOriginIsCurrent(currentSource,submitted.origin)) { setError('This profile changed elsewhere. Cancel edits to load the saved version.'); return; }
    m.send({type: 'SAVE'});
    const request = m.get().context.request;
    try {
      const p = dyn && dctx ? {...scopedSave(dyn, submitted, dctx), applicability: submitted.applicability} as UserPreset
        : {...saveProfile(s, type, currentSource!.name, profileKey(currentSource!), submitted.values), id: profileKey(currentSource!), applicability:submitted.applicability};
      await commitProfileRecords([p],{apply:submitted.staged ? state=>acknowledgeProjectProfileChanges(state,submitted.staged!,submitted.values) : undefined});
      const saved=getState(),origin=findProfile(saved,type,profileKey(p)),acknowledged=resolvePreset(saved,type,profileKey(p));
      if(!origin || acknowledged.error || !profileOriginIsCurrent(origin,submitted.origin))throw new Error('The profile changed again while saving. Your edits are kept.');
      const next=acknowledgedProfileDraft(latest.current,submitted,origin,acknowledged.config);
      update(key,next); m.send({type: 'SAVED', request}); toast(`Saved ${profileName}`);
    } catch (e) { m.send({type: 'FAIL', request, error: e instanceof Error ? e.message : String(e)}); }
  };
  const renamable = !!source && source.kind !== 'master';
  const naming = snap.value === 'naming' || snap.value === 'renaming' ? snap.context.name : null, nameError = naming === null ? null : snap.context.nameError;
  const nameInput = useRef<HTMLInputElement>(null);
  const startRename = () => {if (renamable && m.can({type: 'RENAME', name: profileName})) m.send({type: 'RENAME', name: profileName});};
  useEffect(() => {if (renameRequest?.key === name && Date.now() - renameRequest.at < 2000) startRename();}, [renameRequest]);
  const writeName = async (next: string) => {
    const current = findProfile(getState(), type, name);
    if (!current) throw new Error('This profile is unavailable.');
    const prior = current.name;
    await commitProfileRecords([renameOwnedProfile(current, next)]);
    const renamed = findProfile(getState(), type, name);
    if (draft && renamed) update(key, {...latest.current, origin: renamed});
    toast(`Renamed to ${next}`, {label: 'Undo', run: () => {
      if (!m.can({type: 'RENAME', name: next})) {toast('Finish saving first, then rename it back.'); return;}
      m.send({type: 'RENAME', name: next}); m.send({type: 'TYPE', value: prior}); void commitName();
    }});
  };
  const commitName = async () => {
    const v = m.get().value;
    if (v === 'renaming') {m.send({type: 'COMMIT', value: '', current: '', taken: false}); return;}
    if (v !== 'naming') return;
    const s = getState(), current = findProfile(s, type, name), value = m.get().context.name, next = value.trim();
    if (!current) return;
    const taken = s.userPresets.some((p) => p.type === type && !p.archived && profileKey(p) !== profileKey(current) && p.name.toLocaleLowerCase() === next.toLocaleLowerCase());
    m.send({type: 'COMMIT', value, current: current.name, taken});
    if (m.get().value !== 'renaming') return;
    const request = m.get().context.request;
    try {await writeName(next); m.send({type: 'RENAMED', request});}
    catch (e) {m.send({type: 'RENAME_FAIL', request, error: e instanceof Error ? e.message : String(e)});}
  };
  if (!resolved.user || !resolved.base || resolved.error) return <section className="pf-full-editor"><div className="pf-editor-error" role="alert"><strong>{resolved.user?.name ?? 'Profile unavailable'}</strong><p>{resolved.error ?? 'This profile could not be loaded.'}</p><button type="button" className="btn" onClick={() => void (async () => {try {const {loadCatalog}=await import('../actions');await loadCatalog();const s=getState();if(s.profileLibraryLoad.state==='failed')throw new Error(s.profileLibraryLoad.error);if(!findProfile(s,type,name))throw new Error('Profile is still unavailable.');} catch(e) {setError(e instanceof Error ? e.message : String(e));}})()}>Retry</button>{error && <p>{error}</p>}</div><button type="button" className="btn-ghost" onClick={onBack}>Close profiles</button></section>;
  return <DetailPane label={`Edit ${profileName}`} error={error}
    identity={<>{type === 'filament' && <ColorPicker value={profileColour(values) ?? ''} label="Default filament color" disabled={saving} onCommit={colour=>set('filament_colour',Array.isArray(values.filament_colour) ? [colour] : colour)}/>}{naming !== null ? <input ref={nameInput} autoFocus onFocus={e => e.currentTarget.select()} className="pf-name-input" aria-label="Profile name" aria-invalid={!!nameError} aria-describedby={nameError ? `${key}-name-error` : undefined} value={naming} autoComplete="off" spellCheck={false}
        readOnly={snap.value === 'renaming'} onChange={e => m.send({type: 'TYPE', value: e.target.value})} onBlur={() => void commitName()}
        onKeyDown={e => {if (e.key === 'Enter') {e.preventDefault(); void commitName();} else if (e.key === 'Escape' && snap.value === 'naming') {e.preventDefault(); e.stopPropagation(); m.send({type: 'CANCEL'});}}}/>
        : <span className="pf-editor-name"><strong onClick={renamable ? startRename : undefined} data-renamable={renamable || undefined}>{profileName}</strong>{renamable && <button type="button" className="reset-icon pf-rename" aria-label="Rename" disabled={saving} onClick={startRename}><Glyph name="edit" size={14}/></button>}</span>}</>}
    summary={nameError ? <div className="pf-editor-summary pf-name-error" id={`${key}-name-error`} role="alert">{nameError}</div> : <div className="pf-editor-summary">{profileSummary(type, values)}{draft?.staged ? ' · Includes project changes' : ''}</div>}
    controls={<><button type="button" className={`btn pf-editor-save${dirty ? ' primary' : ''}`} disabled={saving || !dirty || !ready} onClick={() => void save()}>{saving ? 'Saving…' : 'Save'}</button>
        {dirty && !foldActions && <button type="button" className="btn" disabled={saving} onClick={revert}>Revert</button>}
        {onDuplicate && !foldActions && <button type="button" className="btn" disabled={saving || dirty || !ready} onClick={onDuplicate}>Duplicate</button>}
        <MenuButton label="Profile actions" className="icon-btn" items={[...(renamable ? [{label: 'Rename', disabled: saving, action: startRename}] : []), ...(source ? baseMenuItems(type, profileKey(source)).map((i) => ({...i, disabled: saving || dirty || i.disabled})) : []), ...(source && chainIsDynamic(getState(), type, profileKey(source)) ? [{label: 'Split into normal profiles…', disabled: saving || dirty, action: () => openSplit(type, profileKey(source))}] : []), ...(foldActions && dirty ? [{label: 'Revert', disabled: saving, action: revert}] : []), ...(foldActions && onDuplicate ? [{label: 'Duplicate', disabled: saving || dirty || !ready, action: onDuplicate}] : []), {label: type === 'filament' ? 'Use in slot 1' : 'Use in project', disabled:saving || dirty, action:()=>requestPreset(type,0,key && profileKey(source!))}, {label:'Export',action:()=>void import('../preset-io').then(m=>m.exportPresetJson(source!))}, {label: 'Reset to base settings', disabled: saving || !resolved.base, action: () => {if(resolved.base) update(key, {...latest.current, values: resolved.base.config});}}, 'sep', {label:'Delete',disabled:saving,action:onRemove}]}>⋯</MenuButton></>}>
    {dctx && Object.keys(changes).length > 0 && <div className="pf-apply-to"><span>Apply changes to</span>
      <Choice label="Apply changes to" value={draft?.applyTo ?? ('' as DynamicScope)} options={scopes.map((id) => ({id, label: scopeLabel(id, dctx)}))} onChange={(applyTo) => update(key, {...latest.current, applyTo})}/></div>}
    {dyn && conflicts.length > 0 && <div className="pf-conflicts" role="status">{conflicts.slice(0, 3).map((c) => {
      const winner = dyn.dynamic.layers.find((l) => l.id === c.winner)!, loser = dyn.dynamic.layers.find((l) => l.id === c.loser)!, both = {...loser.when, ...winner.when};
      return <div key={`${c.key}:${c.winner}:${c.loser}`} className="pf-conflict-row"><span>{OPTIONS[c.key]?.label || c.key}: {whenText(winner.when)} wins over {whenText(loser.when)}</span>
        <button type="button" className="link" disabled={saving || dirty} onClick={() => resolveConflict(c.key, both, loser.values[c.key])}>Use {whenText(loser.when)}</button>
        <button type="button" className="link" disabled={saving || dirty} onClick={() => resolveConflict(c.key, both, winner.values[c.key])}>Keep {whenText(winner.when)}</button></div>;
    })}{conflicts.length > 3 && <span className="pf-conflict-more">{conflicts.length - 3} more</span>}</div>}
    <fieldset className="pf-editor-fields" disabled={saving} aria-busy={saving}><SettingsTab type={type} hidePreset editor={editor} auxiliary={!source ? undefined : type !== 'machine' ? {label:'Works with',icon:'param_dependencies_printers',modified:applicabilityDirty,initial:openOnWorksWith,content:<WorksWith type={type} profile={profileKey(source)} source={source} config={values} value={applicability} onChange={next=>update(key,{...latest.current,applicability:next})}/>}
      : connections ? {label:'Connection',icon:'printer_in_lan',content:<Suspense fallback={null}><ConnectionEditor printer={profileKey(source)} name={profileName}/></Suspense>} : undefined}/></fieldset>
  </DetailPane>;
}

function DetailPane({label, identity, summary, controls, error, children}: {label: string; identity: ReactNode; summary: ReactNode; controls: ReactNode; error?: string | null; children: ReactNode}) {
  return <section className="pf-full-editor" aria-label={label}>
    <div className="pf-editor-heading"><div className="pf-editor-identity">{identity}{summary}</div><div className="pf-editor-controls">{controls}</div></div>
    {error && <div className="pf-editor-error" role="alert">{error}</div>}
    {children}
  </section>;
}
const noop = () => {};
const mmText = (n: number) => String(Math.round(n * 1000) / 1000);
const whenText = (w: DynamicWhen) => [w.printerModel, w.nozzle !== undefined ? `${mmText(w.nozzle)} nozzle` : '', w.layerHeight !== undefined ? `${w.layerHeight.toFixed(2)} layer` : ''].filter(Boolean).join(' × ');
const scopeLabel = (scope: DynamicScope, c: DynamicContext) => scope === 'all' ? 'All' : scope === 'nozzle' ? `${mmText(c.nozzle)} nozzle` : scope === 'layer' ? `${c.layerHeight.toFixed(2)} layer` : `${mmText(c.nozzle)} × ${c.layerHeight.toFixed(2)}`;
export function MasterPresetDetail({type, preset, name, config, identity, controls}: {type: PresetType; preset: string; name: string; config: Config; identity?: ReactNode; controls: ReactNode}) {
  const mode = useStore(effectiveMode);
  const editor: ProfileFields = useMemo(() => ({name: preset, config, baseline: config, changes: {}, set: noop, reset: noop, mode}), [preset, config, mode]);
  return <DetailPane label={name} identity={<>{identity}<span className="pf-editor-name"><strong>{name}</strong></span></>}
    summary={<div className="pf-editor-summary">{profileSummary(type, config)}</div>} controls={controls}>
    <fieldset className="pf-editor-fields"><SettingsTab type={type} hidePreset readOnly editor={editor}
      auxiliary={type !== 'machine' ? {label: 'Works with', icon: 'param_dependencies_printers', content: <WorksWith type={type} profile={preset} config={config}/>} : undefined}/></fieldset>
  </DetailPane>;
}
