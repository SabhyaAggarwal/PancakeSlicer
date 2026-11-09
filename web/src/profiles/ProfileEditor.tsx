import { Suspense, useCallback, useEffect, useMemo, useRef, type ReactNode } from 'react';
import { preloadable } from '../preloadable';
import type { Config, ConfigValue, PresetType, UserPreset } from '../api/types';
import { SettingsTab, type ProfileFields } from '../components/SettingsTab';
import { effectiveMode, getState, resolvePreset, toast, useStore } from '../state/store';
import { changedKeys, resolveProfile, saveProfile } from './model';
import { commitProfileRecords, profileOriginIsCurrent } from './transactions';
import { findProfile, profileKey } from './identity';
import { ColorPicker } from '../components/ColorPicker';
import { profileColour,profileSummary } from './metadata';
import { MenuButton } from '../components/Menu';
import { applicabilityKey } from './WorksWith';
import { Fits } from './Fits';
import { NozzleStrip } from './NozzleStrip';
import type { ProfileApplicability } from './applicability';
import { acknowledgeProjectProfileChanges,type ProjectProfileChanges } from './project-draft';
import { requestPreset } from '../components/PresetCombo';
import { sameValue } from '../state/value-equality';
import { renameOwnedProfile } from './material-library';
import { Glyph } from '../components/Glyph';
import { useMachine } from '../flow/fsm';
import { createProfileSave } from '../flow/profile-save';
import { settingSources } from './bases';
import { DEFAULT_SCALES, effectiveShells, scaledValue, scaleValues, type ScaleRow } from './auto-rules';
import { confirmDialog } from '../components/Ask';
import { autoOf } from './save-to-profile';
import { DynamicTab } from './DynamicTab';
import { PrintOverrides } from './PrintOverrides';
import { newSpeedModel, speedModelOf, SPEED_KEYS, type SpeedModel } from './speed-rules';
import { dynamicSpeeds, effectiveLayerHeight, type SettingOrigin } from './context';
import { DynamicBadge } from '../components/DynamicBadge';
import { Sep, joinParts } from '../separator';
const ConnectionEditor = preloadable(() => import('../printers/ConnectionEditor').then(m => ({default: m.ConnectionEditor})));
export interface ProfileDraft { speedModel?: SpeedModel | null; speedOverrides?: Config; scales?: ScaleRow[];  printOverrides?: Config; values: Config; baseline: Config; origin: UserPreset | undefined; printer: string; applicability?: ProfileApplicability; baselineApplicability?: ProfileApplicability;staged?:ProjectProfileChanges}
export function acknowledgedProfileDraft(current:ProfileDraft,submitted:ProfileDraft,origin:UserPreset,baseline:Config):ProfileDraft|null {
  const keys=new Set([...Object.keys(current.values),...Object.keys(submitted.values)]);
  const unchanged=[...keys].every(key=>sameValue(key,current.values[key],submitted.values[key]));
  if(unchanged && JSON.stringify(current.scales)===JSON.stringify(submitted.scales) && JSON.stringify(current.speedModel)===JSON.stringify(submitted.speedModel) && JSON.stringify(current.speedOverrides)===JSON.stringify(submitted.speedOverrides) && JSON.stringify(current.printOverrides)===JSON.stringify(submitted.printOverrides) && applicabilityKey(current.applicability)===applicabilityKey(submitted.applicability) && current.staged===submitted.staged && current.printer===submitted.printer)return null;
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
  const resolved = useMemo(() => resolveProfile(getState(), type, name), [users, machines, filaments, processes, type, name]);
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
  const latest = useRef<ProfileDraft>({values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter, applicability, baselineApplicability,staged:draft?.staged}); latest.current = {values, baseline, origin: draft?.origin ?? source, printer: draft?.printer ?? ownerPrinter, applicability, baselineApplicability,staged:draft?.staged,scales:draft?.scales,printOverrides:draft?.printOverrides,speedModel:draft?.speedModel,speedOverrides:draft?.speedOverrides};
  const set = useCallback((k: string, value: ConfigValue) => { const current = latest.current; update(key, {...current, values: {...current.values, [k]: value}}); }, [key, update]);
  const reset = useCallback((k: string) => { const current = latest.current; const next = {...current.values}; if (current.baseline[k] === undefined) delete next[k]; else next[k] = current.baseline[k]; update(key, {...current, values: next}); }, [key, update]);
  const resetToBase = useCallback((k: string) => { const current = latest.current, next = {...current.values}, native = resolved.base?.config ?? {}; if (native[k] === undefined) delete next[k]; else next[k] = native[k]; update(key, {...current, values: next}); }, [key, update, resolved.base]);
  const savedScales = (source as {auto?: {scales?: ScaleRow[]}} | undefined)?.auto?.scales ?? [];
  const scales = type === 'process' ? draft?.scales ?? savedScales : [];
  const savedSpeedModel = speedModelOf(source), speedModel = type === 'process' ? draft?.speedModel === null ? undefined : draft?.speedModel ?? savedSpeedModel : undefined;
  const savedSpeedOverrides = Object.fromEntries(Object.entries(source?.overlay ?? source?.overrides ?? {}).filter(([k]) => SPEED_KEYS.has(k)));
  const speedOverrides = draft?.speedOverrides ?? {...savedSpeedOverrides, ...Object.fromEntries(Object.entries(changes).filter(([k]) => SPEED_KEYS.has(k)))};
  const dynamic = scales.length > 0 || !!speedModel;
  const speedDirty = JSON.stringify(speedModel) !== JSON.stringify(savedSpeedModel) || JSON.stringify(speedOverrides) !== JSON.stringify(savedSpeedOverrides);
  const useMultiplier = (k: string) => {const next = {...speedOverrides}; delete next[k]; update(key, {...latest.current, speedOverrides: next});};
  const setSpeedModel = (speedModel: SpeedModel) => update(key, {...latest.current, speedModel});
  const scalesDirty = JSON.stringify(scales) !== JSON.stringify(type === 'process' ? savedScales : []);
  const nozzle = useStore((st) => Number([resolvePreset(st, 'machine', st.printer).config.nozzle_diameter].flat()[0]) || 0.4);
  const height = useStore((st) => effectiveLayerHeight(st) ?? 0.2);
  const heightLimits = useStore((st) => { const c = resolvePreset(st, 'machine', st.printer).config; return `${[c.min_layer_height].flat()[0] ?? ''},${[c.max_layer_height].flat()[0] ?? ''}`; });
  const [minHeight, maxHeight] = heightLimits.split(',').map((v) => Number(v) || undefined);
  const savedOverrides = (source as {printOverrides?: Config} | undefined)?.printOverrides ?? {};
  const overrides = type === 'machine' ? draft?.printOverrides ?? savedOverrides : {};
  const overridesDirty = type === 'machine' && JSON.stringify(overrides) !== JSON.stringify(savedOverrides);
  const setOverrides = useCallback((next: Config) => update(key, {...latest.current, printOverrides: next}), [key, update]);
  const setScales = useCallback((next: ScaleRow[]) => update(key, {...latest.current, scales: next}), [key, update]);
  const makeDynamic = () => { const current = latest.current, next = {...current.values}, native = resolved.base?.config ?? {};
    for (const r of DEFAULT_SCALES) { if (native[r.key] === undefined) delete next[r.key]; else next[r.key] = native[r.key]; }
    update(key, {...current, values: next, scales: [...DEFAULT_SCALES], speedModel: newSpeedModel()}); };
  const makeRegular = async () => {
    if (!await confirmDialog('Make regular', `Lock in the calculated values for a ${nozzle} mm nozzle and ${height.toFixed(2)} mm layer height?`, ['Make regular', 'Cancel'])) return;
    const current = latest.current;
    update(key, {...current, values: {...current.values, ...scaleValues(scales, {nozzle, layerHeight: height}), ...speedResolution.values, layer_height: String(height)}, scales: [], speedModel: null}); };
  const printer = useStore((st) => findProfile(st, 'machine', st.printer));
  const fromPrinter = type === 'process' ? (printer as {printOverrides?: Config} | undefined)?.printOverrides ?? {} : {};
  const printerName = printer?.name ?? '';
  const shown0 = useMemo(() => scales.length ? {...values, ...Object.fromEntries(scales.map((r) => [r.key, scaledValue(r, {nozzle, layerHeight: height})]))} : values, [values, scales, nozzle, height]);
  const shownBase = useMemo(() => { const own = source?.overlay ?? {}, out: Config = {...shown0, ...(type === 'process' ? {layer_height: String(height)} : {})}; for (const [k, v] of Object.entries(fromPrinter)) if (!(k in own) && !(k in changes)) out[k] = v; return out; }, [shown0, fromPrinter, source, changes, height, type]);
  const speedState = useStore(st => st);
  const speedResolution = useMemo(() => {
    const overlay = {...source?.overlay, ...changes};
    for (const k of SPEED_KEYS) {delete overlay[k]; if (speedOverrides[k] !== undefined) overlay[k] = speedOverrides[k];}
    return dynamicSpeeds(speedState, {...resolved, source: source ? {...source, overlay, auto: {schema: 3, scales, speedModel}} as UserPreset : undefined}, shownBase, {}, speedModel);
  }, [speedState, resolved, source, changes, scales, shownBase, speedModel, speedOverrides]);
  const shown = useMemo(() => ({...shownBase, ...speedResolution.values}), [shownBase, speedResolution]);
  const lifted = useMemo(() => type !== 'process' ? {} : Object.fromEntries((['top', 'bottom'] as const).flatMap((side) => {
    const e = effectiveShells(shown, side, height);
    return e.lifted ? [[`${side}_shell_layers`, `${e.effective} at ${height} mm to keep ≥ ${e.thickness} mm`]] : [];
  })), [type, shown, height]);
  const sourceText = useMemo(() => ({...Object.fromEntries(scales.map((r) => [r.key, r.basis === 'nozzle' ? `${r.factor} × nozzle ${nozzle}` : r.basis === 'layer' ? `${r.factor} × layer height ${height}` : `⌈ ${r.factor} mm ÷ layer height ${height} ⌉`])), ...Object.fromEntries(Object.keys(fromPrinter).map((k) => [k, `Set by ${printerName}`])), ...lifted, ...Object.fromEntries(Object.entries(speedResolution.details).map(([key, d]) => [key, joinParts([`${d.speed} mm/s`, d.override && 'Absolute override', d.limit && `capped by ${d.limit.toLowerCase()}`])]))}), [scales, nozzle, height, fromPrinter, printerName, lifted, speedResolution]);
  const sources = useMemo(() => {
    const base = settingSources(getState(), type, name);
    if (type !== 'process') return base;
    const out: Record<string, SettingOrigin> = {};
    for (const [k, v] of Object.entries(base)) if (v !== 'auto') out[k] = v;
    for (const r of scales) out[r.key] = 'auto';
    for (const k of Object.keys(changes)) if (!scales.some((r) => r.key === k)) out[k] = 'set';
    const own = (source as {overlay?: Config} | undefined)?.overlay ?? {};
    for (const k of Object.keys(fromPrinter)) if (!(k in own) && !(k in changes)) out[k] = 'printer';
    for (const [k, d] of Object.entries(speedResolution.details)) out[k] = d.override ? 'set' : 'auto';
    for (const k of Object.keys(lifted)) if (!(k in changes)) out[k] = 'auto';
    return out;
  }, [type, name, users, scales, changes, fromPrinter, source, lifted, speedResolution]);
  const setOrUnlink = useCallback((k: string, value: ConfigValue) => { const current = latest.current, rows = current.scales ?? savedScales; update(key, {...current, values: {...current.values, [k]: value}, ...(rows.some((r) => r.key === k) ? {scales: rows.filter((r) => r.key !== k)} : {})}); }, [key, update, savedScales]);
  const relink = useCallback((k: string): void => {
    const row = savedScales.find((r) => r.key === k) ?? DEFAULT_SCALES.find((r) => r.key === k);
    if (type === 'process' && speedModel && SPEED_KEYS.has(k)) {useMultiplier(k); return;}
    if (type !== 'process' || !row) {resetToBase(k); return;}
    const current = latest.current, next = {...current.values}; if (current.baseline[k] === undefined) delete next[k]; else next[k] = current.baseline[k];
    update(key, {...current, values: next, scales: [...(current.scales ?? savedScales).filter((r) => r.key !== k), row]});
  }, [type, key, update, savedScales, resetToBase, speedModel, useMultiplier]);
  const editor: ProfileFields = useMemo(() => ({name, readOnlyKeys: dynamic ? SPEED_KEYS : undefined, config: shown, baseline, changes, set: type === 'process' ? setOrUnlink : set, reset, mode: type === 'process' ? 'develop' : mode, sources, sourceText, resetToBase: type === 'process' ? relink : resetToBase}), [name, shown, baseline, changes, set, setOrUnlink, reset, mode, type, sources, sourceText, resetToBase, relink, dynamic]);
  const dirty = Object.keys(changes).length > 0 || applicabilityDirty || scalesDirty || speedDirty || overridesDirty || !!draft?.staged;
  const revert = () => {update(key, null); setError(null);};
  const save = async () => {
    if (!m.can({type: 'SAVE'}) || !dirty) return;
    const s = getState(), currentSource=findProfile(s,type,name);
    const submitted={...latest.current,values:structuredClone(latest.current.values),applicability:structuredClone(latest.current.applicability)};
    if (!s.api) {setError('Offline right now—your edits are safely kept in this tab.'); return;}
    if (!source || resolved.error || !resolved.base) {setError(resolved.error ?? 'That profile isn\'t available right now, but your edits are safely kept here.'); return;}
    if (!profileOriginIsCurrent(currentSource,submitted.origin)) { setError('Heads up: this profile was updated in another tab. Cancel your current edits to load the newest saved version.'); return; }
    m.send({type: 'SAVE'});
    const request = m.get().context.request;
    try {
      const auto = type === 'process' ? autoOf(currentSource, submitted.scales ?? savedScales, submitted.speedModel).auto : undefined;
      const p = {...saveProfile(s, type, currentSource!.name, profileKey(currentSource!), submitted.values), id: profileKey(currentSource!), applicability:submitted.applicability, ...(auto ? {auto} : {}), ...(type === 'machine' && (submitted.printOverrides || Object.keys(savedOverrides).length) ? {printOverrides: submitted.printOverrides ?? savedOverrides} : {})} as UserPreset;
      for (const [k, v] of Object.entries(currentSource?.overlay ?? {})) if (sameValue(k, submitted.values[k], submitted.baseline[k])) {
        p.overlay![k] = p.overrides[k] = v;
        (p.baseline ??= {})[k] = currentSource?.baseline?.[k] ?? resolved.base!.config[k] ?? null;
      }
      if (type === 'process' && (submitted.scales?.length || speedModel)) for (const k of SPEED_KEYS) {
        delete p.overlay?.[k]; delete p.overrides[k];
        const explicit = submitted.speedOverrides ?? {...savedSpeedOverrides, ...Object.fromEntries(Object.entries(changes).filter(([k]) => SPEED_KEYS.has(k)))};
        if (explicit[k] !== undefined) p.overlay![k] = p.overrides[k] = explicit[k];
      }
      if (type === 'process' && !auto) delete (p as {auto?: unknown}).auto;
      await commitProfileRecords([p],{apply:submitted.staged ? state=>acknowledgeProjectProfileChanges(state,submitted.staged!,submitted.values) : undefined});
      const saved=getState(),origin=findProfile(saved,type,profileKey(p)),acknowledged=resolveProfile(saved,type,profileKey(p));
      if(!origin || acknowledged.error || !profileOriginIsCurrent(origin,submitted.origin))throw new Error('This profile was updated elsewhere while you were saving. Don\'t worry, your edits are still here!');
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
    if (!current) throw new Error('Oops! This profile isn\'t available right now.');
    const prior = current.name;
    await commitProfileRecords([renameOwnedProfile(current, next)]);
    const renamed = findProfile(getState(), type, name);
    if (draft && renamed) update(key, {...latest.current, origin: renamed});
    toast(`Renamed to ${next}`, {label: 'Undo', run: () => {
      if (!m.can({type: 'RENAME', name: next})) {toast('Wait a second for the current save to finish, then you can rename it.'); return;}
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
  if (!resolved.user || !resolved.base || resolved.error) return <section className="pf-full-editor"><div className="pf-editor-error" role="alert"><strong>{resolved.user?.name ?? 'Profile unavailable'}</strong><p>{resolved.error ?? 'Oops! We couldn\'t load that profile.'}</p><button type="button" className="btn" onClick={() => void (async () => {try {const {loadCatalog}=await import('../actions');await loadCatalog();const s=getState();if(s.profileLibraryLoad.state==='failed')throw new Error(s.profileLibraryLoad.error);if(!findProfile(s,type,name))throw new Error('That profile is still unavailable.');} catch(e) {setError(e instanceof Error ? e.message : String(e));}})()}>Retry</button>{error && <p>{error}</p>}</div><button type="button" className="btn-ghost" onClick={onBack}>Close profiles</button></section>;
  return <DetailPane label={`Edit ${profileName}`} error={error}
    identity={<>{type === 'filament' && <ColorPicker value={profileColour(values) ?? ''} label="Default filament color" disabled={saving} onCommit={colour=>set('filament_colour',Array.isArray(values.filament_colour) ? [colour] : colour)}/>}{naming !== null ? <input ref={nameInput} autoFocus onFocus={e => e.currentTarget.select()} className="pf-name-input" aria-label="Profile name" aria-invalid={!!nameError} aria-describedby={nameError ? `${key}-name-error` : undefined} value={naming} autoComplete="off" spellCheck={false}
        readOnly={snap.value === 'renaming'} onChange={e => m.send({type: 'TYPE', value: e.target.value})} onBlur={() => void commitName()}
        onKeyDown={e => {if (e.key === 'Enter') {e.preventDefault(); void commitName();} else if (e.key === 'Escape' && snap.value === 'naming') {e.preventDefault(); e.stopPropagation(); m.send({type: 'CANCEL'});}}}/>
        : <span className="pf-editor-name"><strong onClick={renamable ? startRename : undefined} data-renamable={renamable || undefined}>{profileName}</strong>{dynamic && <DynamicBadge/>}{renamable && <button type="button" className="reset-icon pf-rename" aria-label="Rename" disabled={saving} onClick={startRename}><Glyph name="edit" size={14}/></button>}</span>}</>}
    summary={nameError ? <div className="pf-editor-summary pf-name-error" id={`${key}-name-error`} role="alert">{nameError}</div> : <div className="pf-editor-summary">{type === 'process' ? autoPreferences(source) : profileSummary(type, values)}{draft?.staged && <><Sep/>Includes project changes</>}</div>}
    controls={<>{type === 'machine' && source && <NozzleStrip printerKey={profileKey(source)} disabled={saving || dirty || !ready}/>}{(dirty || saving) && <button type="button" className="btn primary pf-editor-save" disabled={saving || !ready} onClick={() => void save()}>{saving ? 'Saving…' : 'Save'}</button>}
        {dirty && !foldActions && <button type="button" className="btn" disabled={saving} onClick={revert}>Revert</button>}
        {onDuplicate && !foldActions && <button type="button" className="btn" disabled={saving || dirty || !ready} onClick={onDuplicate}>Duplicate</button>}
        <MenuButton label="Profile actions" className="icon-btn" items={[...(renamable ? [{label: 'Rename', disabled: saving, action: startRename}] : []), ...(type === 'process' && source ? [dynamic ? {label: 'Make regular', disabled: saving, action: () => void makeRegular()} : {label: 'Make dynamic', disabled: saving, action: makeDynamic}] : []), ...(foldActions && dirty ? [{label: 'Revert', disabled: saving, action: revert}] : []), ...(foldActions && onDuplicate ? [{label: 'Duplicate', disabled: saving || dirty || !ready, action: onDuplicate}] : []), {label: type === 'filament' ? 'Use in slot 1' : 'Use in project', disabled:saving || dirty, action:()=>requestPreset(type,0,key && profileKey(source!))}, {label:'Export',action:()=>void import('../preset-io').then(m=>m.exportPresetJson(source!))}, {label: 'Reset to base settings', disabled: saving || !resolved.base, action: () => {if(resolved.base) update(key, {...latest.current, values: resolved.base.config});}}, 'sep', {label:'Delete',disabled:saving,action:onRemove}]}>⋯</MenuButton></>}>
    <fieldset className="pf-editor-fields" disabled={saving} aria-busy={saving}><SettingsTab type={type} hidePreset editor={editor} auxiliary={!source ? undefined : type === 'process' ? (dynamic ? {label: '✦ Dynamic', modified: scalesDirty || speedDirty, content: <DynamicTab config={shown} speedModel={speedModel} speeds={speedResolution.details} missingReference={speedResolution.missingReference} onSpeedModel={setSpeedModel} onUseMultiplier={useMultiplier} rows={scales} nozzle={nozzle} height={height} minHeight={minHeight} maxHeight={maxHeight} readOnly={saving} onChange={setScales} fromPrinter={{name: printerName, values: fromPrinter}}/>} : undefined) : type === 'filament' ? {label:'Works with',modified:applicabilityDirty,content:<Fits value={applicability} readOnly={saving} onChange={next=>update(key,{...latest.current,applicability:next})}/>}
      : [{label: 'Print overrides', modified: overridesDirty, content: <PrintOverrides value={overrides} readOnly={saving} onChange={setOverrides}/>}, ...(connections ? [{label:'Connection',content:<Suspense fallback={null}><ConnectionEditor printer={profileKey(source)} name={profileName}/></Suspense>}] : [])]}/></fieldset>
  </DetailPane>;
}

export function DetailPane({label, identity, summary, controls, error, children}: {label: string; identity: ReactNode; summary: ReactNode; controls: ReactNode; error?: string | null; children: ReactNode}) {
  return <section className="pf-full-editor" aria-label={label}>
    <div className="pf-editor-heading"><div className="pf-editor-identity">{identity}{summary}</div><div className="pf-editor-controls">{controls}</div></div>
    {error && <div className="pf-editor-error" role="alert">{error}</div>}
    {children}
  </section>;
}
const autoPreferences = (p?: UserPreset) => { const h = (p as {auto?: {preferredHeight?: number}} | undefined)?.auto?.preferredHeight; return h ? `Prefers ${h.toFixed(2)} mm` : ''; };
