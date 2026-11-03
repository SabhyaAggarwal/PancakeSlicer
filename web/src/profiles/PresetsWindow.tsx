import { useEffect, useId, useRef, useSyncExternalStore } from 'react';
import { createSelection, reduceSelection, selectedVisible, type SelectionEvent } from './selection';
import { applyDeleteResult, deleteResult, planDeleteSelected, revalidateDeletePlan, serializeSelected, type DeleteOutcome } from './bulk';
import { findProfile, matchesProfile, profileKey } from './identity';
import { ManagerProfileRow } from './ManagerProfileRow';
import { profileAccess, profileReferenceSnapshot, refreshProfileReferences, subscribeProfileReferences } from './references';
import { changedKeys } from './model';
import { ProfileEditor, type ProfileDraft } from './ProfileEditor';
import { profileSummary } from './metadata';
import { Check } from '../components/Check';
import { Dropdown } from '../components/Dropdown';
import { SectionRule, Segmented } from '../components/kit';
import './presets-window.css';
import { useCallback, useMemo, useState } from 'react';
import type { ConfigValue, PresetType, UserPreset } from '../api/types';
import { accessSnapshot } from '../api/access';
import { OPTIONS } from '../options';
import { compatible, getState, resolvePreset, setState, toast, useStore, type State } from '../state/store';
import { Modal, ModalTitle } from '../components/Modal';
import { confirmDialog } from '../components/Ask';
import { diffRows, useTabLoaded } from '../components/PresetDialogs';
import { display, presetLabel } from './format';
import { changeCount, takeValue } from './stack';
import { requestPreset } from '../components/PresetCombo';
import { createProfileFromBase } from './catalog';
import { listCatalog } from './pickerSource';

const TYPES: [PresetType, string][] = [['machine', 'Printer'], ['filament', 'Filament'], ['process', 'Print settings']];
const shortName = presetLabel;

interface Item { key: string; name: string; mine: boolean; parent?: string; changed: number; slot?: number; user?: UserPreset; note: string }

const slotsOf = (s: State, type: PresetType, name: string) => {
  const profile = findProfile(s, type, name), matches = (key: string) => profile ? matchesProfile(profile, key) : key === name;
  return type === 'machine' ? (matches(s.printer) ? [0] : []) : type === 'process' ? (matches(s.process) ? [0] : []) : s.filamentSlots.flatMap((f,i) => matches(f) ? [i] : []);
};

function noteOf(s: State, type: PresetType, name: string) {
  const c = resolvePreset(s, type, name).config;
  return profileSummary(type, c);
}

function items(s: State, type: PresetType): Item[] {
  const system = type === 'machine' ? s.machines : compatible(s, type);
  const mine = s.userPresets.filter((u) => u.type === type);
  const item = (name: string, isMine: boolean, parent?: string, user?: UserPreset): Item => {
    const slots = slotsOf(s, type, name);
    return { key: user ? profileKey(user) : `base:${type}:${name}`, name, mine: isMine, parent, user, slot: slots[0], note: noteOf(s, type, name), changed: slots.reduce((n, i) => n + changeCount(s, type, i), 0) };
  };
  const inUse = (i: Item) => (i.slot === undefined ? 1 : 0);
  const order = (a: Item, b: Item) => inUse(a) - inUse(b) || (a.name < b.name ? -1 : 1);
  return [
    ...mine.map((u) => item(u.name, true, u.inherits, u)).sort(order),
    ...system.filter((p) => slotsOf(s, type, p.name).length > 0).map((p) => item(p.name, false)).sort(order),
  ];
}

const failed = (action: string, e: unknown) => {
  const message = e instanceof Error ? e.message : String(e);
  if (accessSnapshot()?.message !== message) toast(`${action} failed: ${message}`);
};

async function store(p: UserPreset, from?: string) {
  const s = getState();
  await s.api!.saveUserPreset(p);
  if (from && from !== p.name) await s.api!.deleteUserPreset(p.type, from);
  setState((st) => {
    const userPresets = [...st.userPresets.filter((u) => !(u.type === p.type && (u.name === p.name || u.name === from))), p];
    const move = (n: string) => (from && n === from ? p.name : n);
    const patch: Partial<State> = { userPresets };
    if (p.type === 'machine') patch.printer = move(st.printer);
    else if (p.type === 'process') patch.process = move(st.process);
    else {
      patch.filamentSlots = st.filamentSlots.map(move);
      if (from && st.filamentEdits[from]) {
        const filamentEdits = { ...st.filamentEdits, [p.name]: st.filamentEdits[from] };
        delete filamentEdits[from];
        patch.filamentEdits = filamentEdits;
      }
    }
    return patch;
  });
}

export async function removePreset(type: PresetType, u: UserPreset) {
  await refreshProfileReferences();
  const access = profileAccess(type), references = access.isInUse(profileKey(u));
  if (!access.referencesComplete) return void toast('Saved profile references are unavailable. Refresh or recover project storage before deleting.');
  if (references.length) return void toast(`This profile is used by ${references.map(r => r.label).join(', ')}.`);
  const s = getState();
  const slots = slotsOf(s, type, u.name);
  if (slots.length || s.userPresets.some(p => p !== u && p.type === type && (p.parent === u.name || p.inherits === u.name))) return void toast('This profile is in use. Choose another profile before deleting it.');
  try { await s.api!.deleteUserPreset(type, u.name); } catch (e) { return failed('Delete', e); }
  setState((st) => {
    const patch: Partial<State> = { userPresets: st.userPresets.filter((x) => !(x.type === type && x.name === u.name)) };
    if (type === 'machine' && st.printer === u.name) patch.printer = u.inherits;
    else if (type === 'process' && st.process === u.name) patch.process = u.inherits;
    else if (type === 'filament') patch.filamentSlots = st.filamentSlots.map((f) => (f === u.name ? u.inherits : f));
    return patch;
  });
  toast('Deleted', { label: 'Undo', run: () => void store(u).then(() => setState((st) => (
    type === 'machine' ? { printer: slots.length ? u.name : st.printer } : type === 'process' ? { process: slots.length ? u.name : st.process }
      : { filamentSlots: st.filamentSlots.map((f, i) => (slots.includes(i) ? u.name : f)) })), (e) => failed('Undo', e)) });
}

function AddProfile({ type, done, close }: { type: PresetType; done(name: string): void; close(): void }) {
  const [search, setSearch] = useState('');
  useStore((st) => st.printer);
  const rows = listCatalog(type).filter((r) => `${r.name} ${r.vendor}`.toLowerCase().includes(search.toLowerCase()));
  const add = (base: string) => {
    try { const name = createProfileFromBase(type, base); done(name); } catch (e) { failed('Add', e); }
  };
  return (
    <fieldset className="pf-box pf-details">
      <legend>Add from catalog</legend>
      <input className="text" autoFocus aria-label="Search" placeholder="Search" value={search} onChange={(e) => setSearch(e.target.value)} />
      <div className="pf-scroll pf-list" role="listbox" aria-label="Catalog">
        {rows.map((r) => (
          <div key={r.name} role="option" aria-selected={false} aria-disabled={!!r.reason} title={[r.name, r.vendor, r.reason].filter(Boolean).join(' · ')}
            className={`pf-item${r.reason ? ' is-dim' : ''}`} tabIndex={0} onClick={() => !r.reason && add(r.name)}
            onKeyDown={(e) => e.key === 'Enter' && !r.reason && add(r.name)}>
            <span className="pf-item-name" title={r.name}>{presetLabel(r.name)}</span>
            <span className="pf-dim pf-item-note">{[r.vendor, noteOf(getState(), type, r.name)].filter(Boolean).join(' · ')}</span>
          </div>
        ))}
        {!rows.length && <div className="pf-dim">No results</div>}
      </div>
      <div className="pf-actions"><button type="button" className="btn" onClick={close}>Cancel</button></div>
    </fieldset>
  );
}

function Details({type, item, onCompare, draft, onEdit}: {type: PresetType; item: Item; onCompare(): void; draft?: ProfileDraft; onEdit(): void}) {
  const exportOne = async () => { const {exportPresetJson} = await import('../preset-io'); exportPresetJson(item.user ?? {name: item.name, type, inherits: item.name, overrides: {}}); };
  return <section className="pf-profile-overview"><strong>{item.name}</strong><div className="pf-editor-summary">{item.note}{item.slot !== undefined ? ' · In use' : ''}</div><div className="pf-overview-actions"><button type="button" className="btn profile-edit-action" disabled={!item.user} title={!item.user ? 'Add an owned profile to edit these settings' : `Edit ${item.name}`} onClick={onEdit}>Edit profile</button>
    {item.slot === undefined && <button type="button" className="btn" disabled={!!draft} title={draft ? 'Save or cancel profile edits first' : 'Select this profile for the project'} onClick={() => requestPreset(type, 0, item.key)}>{type === 'filament' ? 'Use in slot 1' : 'Use in project'}</button>}
    <button type="button" className="btn-ghost" onClick={() => void exportOne()}>Export</button>
    <button type="button" className="btn-ghost" disabled title="Select exactly two profiles in the list to compare" onClick={onCompare}>Compare</button>
    {draft && <span className="pf-in-use">Unsaved edits</span>}
  </div></section>;
}

function configOf(s: State, type: PresetType, name: string) {
  const slot = slotsOf(s, type, name)[0];
  if (slot === undefined) return resolvePreset(s, type, name).config;
  const { config } = resolvePreset(s, type, name);
  const edits = type === 'filament' ? s.filamentEdits[name] : s.dirty[type];
  return { ...config, ...edits };
}

function Compare({ type, left, right, names, setRight, close }: { type: PresetType; left: string; right: string; names: string[]; setRight(n: string): void; close(): void }) {
  const ready = useTabLoaded(type);
  const [only, setOnly] = useState(true);
  useStore((s) => s.dirty);
  useStore((s) => s.filamentEdits);
  const s = getState();
  const leftSlot = slotsOf(s, type, left)[0];
  const rows = useMemo(() => (ready ? diffRows(type, configOf(s, type, left), configOf(s, type, right), !only) : []), [ready, type, left, right, only, s.dirty, s.filamentEdits]);
  const changed = rows.filter((r) => r.differs).length;
  let group = '';
  return (
    <fieldset className="pf-box pf-compare">
      <legend>Compare</legend>
      <div className="pf-compare-bar">
        <span>{shortName(left)}</span>
        <span aria-hidden>↔</span>
        <Dropdown className="combo" aria-label="Compare with" value={right} onValueChange={setRight} options={[...names.filter((n) => n !== left).map((n) => ({ value: n, label: shortName(n) }))]} />
        <label className="row"><Check checked={only} onCheckedChange={(next) => setOnly(next)} />Only differences</label>
        <span className="pf-dim">{changed}</span>
        <span className="spacer" />
        <button type="button" className="pf-link" onClick={close}>Close</button>
      </div>
      <div className="pf-scroll pf-diff">
        {!ready ? <div role="status">Loading…</div> : !rows.length ? <div className="pf-dim">No differences</div> : (
          <table className="pf-table">
            <thead><tr><th>Setting</th><th className="n">{shortName(left)}</th><th className="n">{shortName(right)}</th>{leftSlot !== undefined && <th aria-label="Use" />}</tr></thead>
            <tbody>{rows.flatMap((r) => {
              const out = [];
              if (r.group !== group) { group = r.group; out.push(<tr key={`g:${group}`} className="pf-group"><td colSpan={4}>{group}</td></tr>); }
              out.push(
                <tr key={r.key} className={r.differs ? 'is-diff' : undefined} title={r.key}>
                  <td>{r.label}</td>
                  <td className="n">{display(r.key, r.left ?? (OPTIONS[r.key]?.default as ConfigValue))}</td>
                  <td className="n">{display(r.key, r.right ?? (OPTIONS[r.key]?.default as ConfigValue))}</td>
                  {leftSlot !== undefined && <td>{r.differs && <button type="button" className="pf-link" onClick={() => takeValue(type, r.key, (r.right ?? OPTIONS[r.key]?.default) as ConfigValue, leftSlot)}>← Use</button>}</td>}
                </tr>,
              );
              return out;
            })}</tbody>
          </table>
        )}
      </div>
    </fieldset>
  );
}

export default function PresetsWindow({ type: initialType, name, compare, edit = false }: { type: PresetType; name?: string; compare?: string; edit?: boolean }) {
  const [type, setType] = useState(initialType), [search, setSearch] = useState('');
  const [drafts, setDrafts] = useState<Record<string, ProfileDraft>>({});
  const updateDraft = useCallback((key: string, draft: ProfileDraft | null) => setDrafts(prior => {const next = {...prior}; if(draft && Object.keys(changedKeys(draft.values, draft.baseline)).length) next[key] = draft; else delete next[key]; return next;}), []);
  const users = useStore(st => st.userPresets), dirty = useStore(st => st.dirty), filamentEdits = useStore(st => st.filamentEdits);
  const machines = useStore(st => st.machines), processes = useStore(st => st.processes), filaments = useStore(st => st.filaments);
  const printer = useStore(st => st.printer), process = useStore(st => st.process), slots = useStore(st => st.filamentSlots);
  const list = useMemo(() => items(getState(), type), [type, users, dirty, filamentEdits, machines, processes, filaments, printer, process, slots]);
  const visible = useMemo(() => list.filter(i => `${i.name} ${i.note}`.toLocaleLowerCase().includes(search.toLocaleLowerCase())), [list, search]);
  const order = useMemo(() => visible.map(i => i.key), [visible]), scope = `profiles:${type}`;
  const current = (t: PresetType) => (t === 'machine' ? printer : t === 'process' ? process : slots[0]) ?? '';
  const [selectionState, setSelection] = useState(() => {
    let initial = createSelection(scope, order);
    const target = list.find(i => i.name === name || i.key === name || i.key === current(initialType) || i.name === current(initialType));
    if(target) initial = reduceSelection(initial, {type: 'CLICK', key: target.key});
    if(compare !== undefined) {const other = list.find(i => i.name === compare || i.key === compare) ?? list.find(i => i.key !== target?.key); if(other) initial = reduceSelection(initial, {type: 'CLICK', key: other.key, toggle: true});}
    return initial;
  });
  const selection = useMemo(() => reduceSelection(selectionState, {type: 'SYNC', scope, visible: order}), [selectionState, scope, order]);
  useEffect(() => {setSelection(prior => reduceSelection(prior, {type: 'SYNC', scope, visible: order}));}, [scope, order]);
  const selected = selectedVisible(selection), selectedSet = useMemo(() => new Set(selected), [selection]);
  const selectedItems = visible.filter(i => selectedSet.has(i.key)), item = selectedItems.length === 1 ? selectedItems[0] : undefined;
  const latestSelection = useRef(selection); latestSelection.current = selection;
  const id = useId();
  const [editingKey, setEditingKey] = useState<string | null>(() => edit ? list.find(i => i.key === name || i.name === name)?.key ?? null : null);
  const [adding, setAdding] = useState(false), [comparing, setComparing] = useState(compare !== undefined), [busy, setBusy] = useState(false);
  const [bulkError, setBulkError] = useState<string | null>(null);
  const references = useSyncExternalStore(subscribeProfileReferences, profileReferenceSnapshot);
  const dispatch = useCallback((event: SelectionEvent, focus = false) => {
    const next = reduceSelection(latestSelection.current, event); latestSelection.current = next; setSelection(next); setAdding(false); setComparing(false); setEditingKey(null);
    if(focus && next.focus) requestAnimationFrame(() => {const row = document.getElementById(`${id}:${next.focus}`); row?.focus({preventScroll: true}); row?.scrollIntoView({block: 'nearest'});});
  }, [id]);
  const access = useMemo(() => profileAccess(type), [type, references, users]);
  const plan = useMemo(() => planDeleteSelected(selection, access), [selection, access]);
  const leave = async (action: () => void) => {if(Object.keys(drafts).length && !await confirmDialog('Unsaved profile edits', 'Discard unsaved profile edits?', ['Discard', 'Keep editing'])) return; action();};
  const close = () => {if(!busy) void leave(() => setState({presetUx: null}));};
  const pickType = (t: PresetType) => {setAdding(false); setType(t); setSearch(''); setComparing(false); setEditingKey(null); setBulkError(null);};
  const exportSelected = async () => {
    setBusy(true); setBulkError(null);
    try {
      const state = getState(), {presetJsonText, downloadArchive} = await import('../preset-io'), {strToU8, zipSync} = await import('fflate');
      const result = serializeSelected(latestSelection.current, {label: access.label, serialize: key => {const p = findProfile(state, type, key); if(!p) throw new Error('Profile unavailable'); return presetJsonText(p, state);}});
      if(result.files.length) downloadArchive({name: 'selected-profiles.zip', data: zipSync(Object.fromEntries(result.files.map(f => [f.filename, strToU8(f.text)])))});
      if(result.failed.length) setBulkError(result.failed.map(f => `${f.name}: ${f.error}`).join('\n'));
    } catch(e) {setBulkError(e instanceof Error ? e.message : String(e));} finally {setBusy(false);}
  };
  const deleteSelected = async () => {
    await refreshProfileReferences();
    const confirmed = planDeleteSelected(latestSelection.current, profileAccess(type));
    if(!confirmed.deletable.length) return;
    const text = [`Delete ${confirmed.deletable.length} profile${confirmed.deletable.length === 1 ? '' : 's'}?`, ...confirmed.confirmation.deleteNames, ...confirmed.blocked.map(p => `${p.name}: ${p.reason}`)].join('\n');
    if(!await confirmDialog('Delete selected profiles', text, ['Delete', 'Cancel'])) return;
    await refreshProfileReferences();
    const final = revalidateDeletePlan(confirmed, latestSelection.current, profileAccess(type)), outcomes: DeleteOutcome[] = [];
    setBusy(true); setBulkError(null);
    try {
      for(const {key, name} of final.deletable) {
        try {await getState().api!.deleteUserPreset(type, name); outcomes.push({key, status: 'deleted'});}
        catch(e) {outcomes.push({key, status: 'unknown', error: e instanceof Error ? e.message : String(e)});}
      }
      const result = deleteResult(final, outcomes);
      setState(latest => ({userPresets: applyDeleteResult(latest.userPresets, result, profileKey)}));
      if(result.failed.length || result.unknown.length || result.notAttempted.length) setBulkError(`${result.deleted.length} deleted; ${result.failed.length + result.unknown.length + result.notAttempted.length} remain. Refresh profile data before retrying.`);
      else toast(`Deleted ${result.deleted.length} profiles`);
    } finally {setBusy(false);}
  };
  const compareReason = selected.length !== 2 ? 'Select exactly two visible profiles to compare' : undefined;
  const deleteReason = plan.deletable.length ? undefined : plan.blocked[0]?.reason ?? 'Select profiles to delete';
  return <Modal className="np-dialog pf-window" label="Profiles" onClose={close} backdropClose onEscapeKeyDown={event => {if(busy || selected.length) {event.preventDefault(); if(!busy) dispatch({type: 'ESCAPE'});}}}>
    <ModalTitle title="Profiles" onClose={close}/>
    <div className="np-dialog-body">
      <div className="pf-tabs"><Segmented label="Profile type" value={type} options={TYPES.map(([id,label]) => ({id,label}))} onChange={v => pickType(v as PresetType)}/><span className="spacer"/><button type="button" className="btn" disabled={busy} onClick={() => setAdding(true)}>Add from catalog</button><button type="button" className="btn" disabled={busy} onClick={() => void leave(() => setState({presetUx: {kind: 'import', back: {kind: 'presets', type, name: item?.name}}}))}>Import Orca / Bambu</button><button type="button" className="btn-ghost" disabled={busy} onClick={() => void leave(() => setState({presetUx: {kind: 'export'}}))}>Export all</button></div>
      <div className="pf-split"><section className="pf-my-profiles" aria-label="Profiles">
        <SectionRule title="Profiles"/>
        <input className="text pf-profile-search" aria-label="Search profiles" placeholder="Search profiles" value={search} onChange={e => setSearch(e.target.value)}/>
        <div className="pf-selection-bar"><span>{selected.length ? `${selected.length} selected` : 'Select profiles'}</span>{selected.length > 0 && <button type="button" className="btn-ghost" onClick={() => dispatch({type: 'CLEAR'})}>Clear</button>}</div>
        <div className="pf-scroll pf-list" role="grid" aria-label="Profiles" aria-multiselectable="true" tabIndex={visible.length ? -1 : 0} onKeyDown={e => {
          if(e.target instanceof Element && e.target.closest('input,textarea,[contenteditable=true]')) return;
          const command = e.ctrlKey || e.metaKey;
          if(command && e.key.toLocaleLowerCase() === 'a') {e.preventDefault(); e.stopPropagation(); dispatch({type: 'SELECT_ALL'});}
          else if(['ArrowDown','ArrowUp','Home','End'].includes(e.key)) {e.preventDefault(); e.stopPropagation(); dispatch({type: 'ARROW', direction: e.key === 'Home' ? 'first' : e.key === 'End' ? 'last' : e.key === 'ArrowUp' ? 'previous' : 'next', shift: e.shiftKey, focusOnly: command}, true);}
          else if((e.key === 'Enter' || e.key === ' ') && e.target instanceof HTMLElement && e.target.dataset.profileKey) {e.preventDefault(); dispatch({type: 'CLICK', key: e.target.dataset.profileKey, toggle: command || e.key === ' ', shift: e.shiftKey});}
        }}>
          {visible.map(i => <ManagerProfileRow key={i.key} item={i} listId={id} selected={selectedSet.has(i.key)} focused={selection.focus === i.key} dispatch={dispatch}/>)}
          {!visible.length && <p className="pf-profile-empty">{search ? 'No matching profiles' : 'Add a profile from the catalog'}</p>}
        </div>
      </section><div className="pf-detail-pane">{adding ? <AddProfile type={type} close={() => setAdding(false)} done={n => {setAdding(false); const p = findProfile(getState(), type, n); if(p) {const key = profileKey(p); const fullOrder = [...order, key]; const initial = createSelection(scope, fullOrder); setSelection(reduceSelection(initial,{type: 'CLICK',key}));}}}/>
        : comparing && selectedItems.length === 2 ? <Compare type={type} left={selectedItems[0].name} right={selectedItems[1].name} names={selectedItems.map(i => i.name)} setRight={() => {}} close={() => setComparing(false)}/>
         : item && editingKey === item.key ? <ProfileEditor key={item.key} type={type} name={item.key} draft={drafts[JSON.stringify([type,item.key])]} update={updateDraft} actions={<button type="button" className="btn-ghost" onClick={() => setEditingKey(null)}>Back to profile</button>}/>
        : item ? <Details key={item.key} type={type} item={item} draft={drafts[JSON.stringify([type,item.key])]} onEdit={() => setEditingKey(item.key)} onCompare={() => setComparing(true)}/>
        : <section className="pf-bulk-summary"><strong>{selected.length ? `${selected.length} profiles selected` : 'Select a profile'}</strong>{selectedItems.map(i => <div key={i.key} className="pf-bulk-name">{i.name}</div>)}</section>}
      </div></div>
      {!editingKey && selected.length > 1 && <div className="pf-bulk-actions"><button type="button" className="btn" disabled={!selected.length || busy} title={!selected.length ? 'Select visible profiles to export' : 'Export selected profiles'} onClick={() => void exportSelected()}>Export selected</button><button type="button" className="btn" disabled={!!compareReason || busy} title={compareReason ?? 'Compare selected profiles'} onClick={() => {setAdding(false); setComparing(true);}}>Compare</button><button type="button" className="btn-ghost pf-delete" disabled={!!deleteReason || busy} title={deleteReason ?? 'Delete selected profiles'} onClick={() => void deleteSelected()}>Delete selected</button>{bulkError && <div role="alert">{bulkError}</div>}</div>}
    </div>
  </Modal>;
}
