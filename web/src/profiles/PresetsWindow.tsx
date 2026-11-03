import { savePreferences, usePreference } from '../saved-preferences';
import { ProfileLibraryNotice } from '../components/ProfileLibraryNotice';
import { serviceRecoverySnapshot, subscribeServiceRecovery } from './service-recovery';
import { ServiceProfileRecoveryPanel } from './ServiceProfileRecoveryPanel';
import { useEffect, useId, useRef, useSyncExternalStore } from 'react';
import { createSelection, reduceSelection, selectedVisible, type SelectionEvent } from './selection';
import { serializeSelected } from './bulk';
import { findProfile, matchesProfile, profileKey } from './identity';
import { Radio } from '../components/Radio';
import { ProfileFilterGroup } from './ProfileFilters';
import { selectCompatibility } from './compatibility';
import { ManagerProfileRow } from './ManagerProfileRow';
import { profileAccess, profileReferenceSnapshot, subscribeProfileReferences } from './references';
import { changedKeys, findCatalogBase } from './model';
import { applicabilityKey } from './WorksWith';
import { archiveProfiles, commitProfileRecords, ensureEditableMaster, restoreProfiles } from './transactions';
import { ProfileEditor, type ProfileDraft } from './ProfileEditor';
import { profileColour,profileSummary } from './metadata';
import { Check } from '../components/Check';
import { Dropdown } from '../components/Dropdown';
import { ActionMenu, MenuButton } from '../components/Menu';
import { Segmented } from '../components/kit';
import './presets-window.css';
import { useCallback, useMemo, useState } from 'react';
import type { Config, ConfigValue, PresetType, SystemPreset, UserPreset } from '../api/types';
import { accessSnapshot } from '../api/access';
import { OPTIONS } from '../options';
import { getState, resolvePreset, setState, toast, useStore, type State } from '../state/store';
import { Modal } from '../components/Modal';
import { SettingsDetail } from '../components/VisibilityControl';
import { Dialog,RadioGroup } from 'radix-ui';
import type { ProjectProfileChanges } from './project-draft';
import { confirmDialog } from '../components/Ask';
import { diffRows, useTabLoaded } from '../components/PresetDialogs';
import { display, presetLabel } from './format';
import { changeCount, takeValue } from './stack';
import { createProfileFromBase, listCatalog, resolveCatalogEntry } from './catalog';

const TYPES: [PresetType, string][] = [['machine', 'Printers'], ['filament', 'Filaments'], ['process', 'Print settings']];
const shortName = presetLabel;
const first = (value: unknown) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '');
const coverOf = (vendor: string, model: string) => vendor && model ? `/profiles/${encodeURIComponent(vendor)}/${encodeURIComponent(model)}_cover.png` : undefined;

interface Item { base?:SystemPreset; key: string; name: string; mine: boolean; parent?: string; changed: number; slot?: number; user?: UserPreset; note: string; vendor: string; model:string; nozzle: string; material: string; cover?: string; swatch?:string|null;reason?: string; config: Config }

const slotsOf = (s: State, type: PresetType, name: string) => {
  const profile = findProfile(s, type, name), matches = (key: string) => profile ? matchesProfile(profile, key) : key === name;
  return type === 'machine' ? (matches(s.printer) ? [0] : []) : type === 'process' ? (matches(s.process) ? [0] : []) : s.filamentSlots.flatMap((f,i) => matches(f) ? [i] : []);
};

function noteOf(s: State, type: PresetType, name: string) {
  const c = resolvePreset(s, type, name).config;
  return profileSummary(type, c);
}

function items(s: State, type: PresetType): Item[] {
  const mine = s.userPresets.filter((u) => u.type === type && !u.archived);
  const item = (name: string, isMine: boolean, parent?: string, user?: UserPreset): Item => {
    const identity = user ? profileKey(user) : name;
    const slots = slotsOf(s, type, identity), resolved = resolvePreset(s, type, identity);
    const vendor = resolved.base?.vendor ?? '', model = first(resolved.config.printer_model) || resolved.base?.model || '';
    return { key: user ? profileKey(user) : `base:${type}:${name}`, name, mine: isMine, parent, user, config:resolved.config,swatch:type === 'filament' ? profileColour(resolved.config) : undefined,vendor:type === 'filament' ? first(resolved.config.filament_vendor) || vendor : vendor, model, nozzle:first(resolved.config.nozzle_diameter), material:first(resolved.config.filament_type), cover:type === 'machine' ? coverOf(vendor,model) : undefined, slot: slots[0], note: noteOf(s, type, identity), changed: slots.reduce((n, i) => n + changeCount(s, type, i), 0) };
  };
  const inUse = (i: Item) => (i.slot === undefined ? 1 : 0);
  const order = (a: Item, b: Item) => inUse(a) - inUse(b) || (a.name < b.name ? -1 : 1);
  return [
    ...mine.map((u) => item(u.name, true, u.inherits, u)).sort(order),
  ];
}

const failed = (action: string, e: unknown) => {
  const message = e instanceof Error ? e.message : String(e);
  if (accessSnapshot()?.message !== message) toast(`${action} failed: ${message}`);
};

async function archiveRecords(type: PresetType, keys: string[]) {
  const s=getState(), rows=keys.flatMap(key=>findProfile(s,type,key) ?? []);
  if(!rows.length)throw new Error('Select an editable profile to delete.');
  if(!await confirmDialog('Remove from library',`Remove ${rows.length === 1 ? rows[0].name : `${rows.length} profiles`} from the library?\n${rows.length > 1 ? rows.map(p=>p.name).join('\n')+'\n' : ''}Saved project settings and references are retained. Undo is available.`,['Remove','Cancel']))return;
  const undo=await archiveProfiles(type,rows.map(profileKey));
  toast(`Removed ${rows.length} profile${rows.length === 1 ? '' : 's'} from library`,{label:'Undo',run:()=>void restoreProfiles(undo).then(()=>toast('Profiles restored')).catch(error=>failed('Undo',error))});
}
export async function removePreset(type: PresetType, profile: UserPreset) {try {await archiveRecords(type,[profileKey(profile)]);} catch(error) {failed('Remove',error);}}

function catalogItems(s: State, type: PresetType): Item[] {
  return listCatalog(s,type,{browseAll:true}).map(entry => {
    const {base}=resolveCatalogEntry(s,type,entry.key), config=base.config;
    return {key:entry.key,base,name:entry.name,mine:false,changed:0,note:profileSummary(type,config),vendor:type === 'filament' ? first(config.filament_vendor) || entry.vendor : entry.vendor,model:first(config.printer_model) || base?.model || '',nozzle:first(config.nozzle_diameter),material:first(config.filament_type),cover:type === 'machine' ? coverOf(entry.vendor,first(config.printer_model) || base?.model || '') : undefined,swatch:type === 'filament' ? profileColour(config) : undefined,reason:entry.compatible.ok ? undefined : entry.compatible.reason,config};
  });
}
function AddProfileDetails({type,item,copy,busy,onAdd}: {type:PresetType;item:Item;copy:boolean;busy:boolean;onAdd(name?:string):void}) {
  const [name,setName]=useState(`${item.name} copy`);
  const library=useStore(s=>s.profileLibraryLoad),blocked=library.state==='ready'?undefined:library.state==='failed'?library.error:'Wait for saved profiles to load before adding.';
  return <section className="pf-add-source" aria-label={copy ? 'Copy existing profile' : 'Add from template'}>
    {item.cover && <img className="pf-source-cover" src={item.cover} alt="" loading="lazy"/>}
    <h2>{item.name}</h2><p>{item.note}</p>
    <p>{copy ? 'Create a separate profile with these saved settings.' : 'Add this template as an editable profile.'}</p>
    {copy && <label className="pf-copy-name">Profile name<input className="text" value={name} onChange={e=>setName(e.target.value)} aria-label="New profile name"/></label>}
    {item.reason && <p role="alert">{item.reason}</p>}
    {blocked && <p role="status">{blocked}</p>}
    <button type="button" className="btn primary" disabled={busy || !!blocked || !!item.reason || copy && !name.trim()} title={blocked ?? item.reason ?? (copy && !name.trim() ? 'Enter a profile name' : undefined)} onClick={()=>onAdd(copy ? name.trim() : undefined)}>{busy ? 'Adding…' : copy ? 'Add copy' : `Add ${type === 'machine' ? 'printer' : type === 'filament' ? 'filament' : 'print settings'}`}</button>
  </section>;
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
        <span>{shortName(resolvePreset(s,type,left).name ?? 'Unavailable profile')}</span>
        <span aria-hidden>↔</span>
        <Dropdown className="combo" aria-label="Compare with" value={right} onValueChange={setRight} options={[...names.filter((n) => n !== left).map((n) => ({ value: n, label: shortName(resolvePreset(s,type,n).name ?? 'Unavailable profile') }))]} />
        <label className="row"><Check checked={only} onCheckedChange={(next) => setOnly(next)} />Only differences</label>
        <span className="pf-dim">{changed}</span>
        <span className="spacer" />
        <button type="button" className="pf-link" onClick={close}>Close</button>
      </div>
      <div className="pf-scroll pf-diff">
        {!ready ? <div role="status">Loading…</div> : !rows.length ? <div className="pf-dim">No differences</div> : (
          <table className="pf-table">
            <thead><tr><th>Setting</th><th className="n">{shortName(resolvePreset(s,type,left).name ?? 'Unavailable profile')}</th><th className="n">{shortName(resolvePreset(s,type,right).name ?? 'Unavailable profile')}</th>{leftSlot !== undefined && <th aria-label="Use" />}</tr></thead>
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

export default function PresetsWindow({ type: initialType, name, compare, edit = false,add = false,stage }: { type: PresetType; name?: string; compare?: string; edit?: boolean;add?:boolean;stage?:ProjectProfileChanges }) {
  const recovery=useSyncExternalStore(subscribeServiceRecovery,serviceRecoverySnapshot,serviceRecoverySnapshot);
  const [type, setType] = useState(initialType), [search, setSearch] = useState(''), [adding, setAdding] = useState(add), [masters,setMasters] = useState(()=>!!name && findProfile(getState(),initialType,name)?.kind === 'master');
  const [addSource,setAddSource]=useState('template');
  const [vendorFilter,setVendorFilter] = useState<string[]>([]), [nozzleFilter,setNozzleFilter] = useState<string[]>([]), [materialFilter,setMaterialFilter] = useState<string[]>([]), [modelFilter,setModelFilter]=useState<string[]>([]), [printerFilter,setPrinterFilter]=useState<string[]>([]);
  const [filtersOpen,setFiltersOpen]=useState(false);
  const split = useRef<HTMLDivElement>(null);
  const savedListWidth = usePreference('orca.profileListWidth');
  const listWidth = Math.max(260,Math.min(480,Number(savedListWidth) || 340));
  const resizeList = (width: number, commit: boolean) => {
    const available = split.current?.clientWidth ?? 1000;
    const next = Math.round(Math.max(240, Math.min(480, available - 420, width)));
    split.current?.style.setProperty('--profile-list-width', `${next}px`);
    if(commit) savePreferences({'orca.profileListWidth':next});
  };
  const [drafts, setDrafts] = useState<Record<string, ProfileDraft>>({});
  const updateDraft = useCallback((key: string, draft: ProfileDraft | null) => setDrafts(prior => {const next = {...prior}; if(draft && (Object.keys(changedKeys(draft.values, draft.baseline)).length || applicabilityKey(draft.applicability) !== applicabilityKey(draft.baselineApplicability) || draft.staged)) next[key] = draft; else delete next[key]; return next;}), []);
  const users = useStore(st => st.userPresets), dirty = useStore(st => st.dirty), filamentEdits = useStore(st => st.filamentEdits);
  const machines = useStore(st => st.machines), processes = useStore(st => st.processes), filaments = useStore(st => st.filaments);
  const printer = useStore(st => st.printer), process = useStore(st => st.process), slots = useStore(st => st.filamentSlots);
  const list = useMemo(() => {
    const s = getState(); if(adding) return addSource === 'copy' ? items(s,type).filter(i=>i.user?.kind !== 'master') : catalogItems(s,type);
    const records = items(s,type).filter(i=>(i.user?.kind === 'master') === masters);
    if(!masters) return records;
    return [...records,...catalogItems(s,type).filter(i=>!s.userPresets.some(p=>p.type === type && p.kind === 'master' && p.base && findCatalogBase(s,type,p.base).base === i.base))];
  }, [adding,addSource,masters,type,users,dirty,filamentEdits,machines,processes,filaments,printer,process,slots]);
  const printerOptions=useMemo(()=>users.filter(p=>p.type === 'machine' && !p.archived && p.kind !== 'master').map(p=>({value:profileKey(p),label:p.name})),[users]);
  const facets=useMemo(()=>{
    const rows=list.filter(i=>!vendorFilter.length || vendorFilter.includes(i.vendor));
    const contexts=machines.filter(p=>!modelFilter.length || modelFilter.includes(first(p.config.printer_model) || p.model || ''));
    const unique=(values:string[])=>[...new Set(values.filter(Boolean))].sort();
    return {vendors:unique(list.map(i=>i.vendor)),models:unique(type === 'machine' ? rows.map(i=>i.model) : machines.map(p=>first(p.config.printer_model) || p.model || '')),nozzles:unique(type === 'machine' ? rows.filter(i=>!modelFilter.length || modelFilter.includes(i.model)).map(i=>i.nozzle) : contexts.map(p=>first(p.config.nozzle_diameter))),materials:unique(rows.map(i=>i.material))};
  },[list,type,machines,vendorFilter,modelFilter]);
  useEffect(()=>{setNozzleFilter(prior=>{const next=prior.filter(n=>facets.nozzles.includes(n));return next.length === prior.length ? prior : next;});},[facets.nozzles]);
  const matching=useMemo(()=>{
    const state=getState();
    const hasContext=type !== 'machine' && (printerFilter.length || modelFilter.length || nozzleFilter.length);
    const contexts=hasContext ? printerFilter.length ? printerFilter : machines.filter(p=>(!modelFilter.length || modelFilter.includes(first(p.config.printer_model) || p.model || '')) && (!nozzleFilter.length || nozzleFilter.includes(first(p.config.nozzle_diameter)))).map(p=>p.name) : [];
    return list.filter(i=>(!vendorFilter.length || vendorFilter.includes(i.vendor)) && (!materialFilter.length || materialFilter.includes(i.material)) && (type === 'machine' ? (!modelFilter.length || modelFilter.includes(i.model)) && (!nozzleFilter.length || nozzleFilter.includes(i.nozzle)) : !hasContext || contexts.some(key=>selectCompatibility(state,type,i.user ? i.key : i.name,key).ok)));
  },[list,type,vendorFilter,materialFilter,modelFilter,nozzleFilter,printerFilter,machines,users,printer,process,dirty,filamentEdits]);
  const visible=useMemo(()=>matching.filter(i=>`${i.name} ${i.note}`.toLocaleLowerCase().includes(search.toLocaleLowerCase())).sort((a,b)=>a.name.localeCompare(b.name)),[matching,search]);
  const filtersActive=vendorFilter.length+modelFilter.length+nozzleFilter.length+materialFilter.length+printerFilter.length;
  const clearFilters=()=>{setVendorFilter([]);setModelFilter([]);setNozzleFilter([]);setMaterialFilter([]);setPrinterFilter([]);};
  const order = useMemo(() => visible.map(i => i.key), [visible]), scope = `profiles:${adding ? `add:${addSource}` : masters ? 'masters' : 'owned'}:${type}`;
  const current = (t: PresetType) => (t === 'machine' ? printer : t === 'process' ? process : slots[0]) ?? '';
  const [selectionState, setSelection] = useState(() => {
    let initial = createSelection(scope, order);
    const target = (name ? list.find(i => i.name === name || i.key === name) : undefined) ?? list.find(i => i.key === current(initialType) || i.name === current(initialType)) ?? list[0];
    if(target) initial = reduceSelection(initial, {type: 'CLICK', key: target.key});
    if(compare !== undefined) {const other = list.find(i => i.name === compare || i.key === compare) ?? list.find(i => i.key !== target?.key); if(other) initial = reduceSelection(initial, {type: 'CLICK', key: other.key, toggle: true});}
    return initial;
  });
  const previousScope = useRef(scope), initializedSelection = useRef(order.length > 0);
  const selection = useMemo(() => reduceSelection(selectionState, {type: 'SYNC', scope, visible: order}), [selectionState, scope, order]);
  useEffect(() => {
    const changed=previousScope.current !== scope; previousScope.current=scope;
    const initialize = changed || !initializedSelection.current;
    if(order.length) initializedSelection.current=true;
    setSelection(prior=>{
      let next=reduceSelection(prior,{type:'SYNC',scope,visible:order});
      if(order.length && next.selected.length === 0 && (initialize || prior.selected.length > 0 && !prior.selected.some(key=>drafts[JSON.stringify([type,key])]))) {
        const selectedKey=current(type), target=visible.find(i=>i.key === name || i.name === name) ?? visible.find(i=>i.key === selectedKey || i.name === selectedKey) ?? visible[0];
        next=reduceSelection(next,{type:'CLICK',key:target.key});
      }
      return next;
    });
  },[scope,order]);
  const selected = selectedVisible(selection), selectedSet = useMemo(() => new Set(selected), [selection]);
  const selectedItems=visible.filter(i=>selectedSet.has(i.key));
  const inspected=useRef<string|null>(null);
  const filteredDraft=inspected.current && !order.includes(inspected.current) && drafts[JSON.stringify([type,inspected.current])] ? list.find(i=>i.key === inspected.current) : undefined;
  const item=filteredDraft ?? (selectedItems.length === 1 ? selectedItems[0] : undefined);
  if(item && !filteredDraft)inspected.current=item.key;
  const latestSelection = useRef(selection); latestSelection.current = selection;
  const selectionRevision=useRef(0);
  const id = useId();
  const editingKey = item?.key ?? null;
  void edit;
  const [comparing, setComparing] = useState(compare !== undefined), [busy, setBusy] = useState(false);
  const [bulkError, setBulkError] = useState<string | null>(null);
  const [contextMenu,setContextMenu] = useState<{x:number;y:number;anchor:HTMLElement}|null>(null);
  const references = useSyncExternalStore(subscribeProfileReferences, profileReferenceSnapshot);
  const dispatch = useCallback((event: SelectionEvent, focus = false) => {
    selectionRevision.current++;initializedSelection.current=true;inspected.current=null;
    const next = reduceSelection(latestSelection.current, event); latestSelection.current = next; setSelection(next); setComparing(false);
    if(focus && next.focus) requestAnimationFrame(() => {const row = document.getElementById(`${id}:${next.focus}`); row?.focus({preventScroll: true}); row?.scrollIntoView({block: 'nearest'});});
  }, [id]);
  const access = useMemo(() => profileAccess(type), [type, references, users]);
  const leave = async (action: () => void) => {if(Object.keys(drafts).length && !await confirmDialog('Unsaved profile edits', 'Discard unsaved profile edits?', ['Discard', 'Keep editing'])) return; action();};
  const close=()=>{if(!busy)void leave(()=>setState({presetUx:null}));};
  const pickType = (t: PresetType, target?: string) => {
    selectionRevision.current++;
    setType(t); setSearch(''); setVendorFilter([]); setNozzleFilter([]); setMaterialFilter([]); setComparing(false); setBulkError(null);
    const rows = items(getState(), t), nextScope = `profiles:${adding ? `add:${addSource}` : masters ? 'masters' : 'owned'}:${t}`;
    let next = createSelection(nextScope, rows.map(i => i.key));
    const row = rows.find(i => i.key === target || i.name === target);
    if(row) next = reduceSelection(next, {type: 'CLICK', key: row.key});
    setSelection(next);
  };
  const added = (n: string) => {setMasters(false); setAdding(false); setSearch(''); setVendorFilter([]); setNozzleFilter([]); setMaterialFilter([]); const p = findProfile(getState(), type, n); if(p) {const key = profileKey(p); const initial = createSelection(`profiles:owned:${type}`, [key]); setSelection(reduceSelection(initial,{type: 'CLICK',key}));}};
  const addSelected = async (item:Item,newName?:string) => {
    setBusy(true);setBulkError(null);
    try {
      const s=getState();
      if(addSource === 'template') {
        added(await createProfileFromBase(type,item.key));
      } else {
        if(!item.user || !newName) throw new Error('Choose a profile and enter a name for its copy.');
        if(s.userPresets.some(p=>p.type === type && p.name === newName)) throw new Error('A profile already uses this name. Choose another name.');
        if(!s.api) throw new Error('Profile service unavailable. Reconnect and try again.');
        const p:UserPreset={...item.user,id:`profile:${crypto.randomUUID()}`,kind:'profile',name:newName,revision:0,aliases:[],archived:undefined,provenance:{source:'profile',sourceId:profileKey(item.user),sourceName:item.user.name,sourceRevision:item.user.revision,operationId:crypto.randomUUID()}};
        await commitProfileRecords([p]);added(profileKey(p));
      }
    } catch(error) {setBulkError(error instanceof Error ? error.message : String(error));}
    finally {setBusy(false);}
  };
  const exportSelected = async () => {
    setBusy(true); setBulkError(null);
    try {
      const state = getState(), {presetJsonText, downloadArchive} = await import('../preset-io'), {strToU8, zipSync} = await import('fflate');
      const result = serializeSelected(latestSelection.current, {label: access.label, serialize: key => {const p = findProfile(state, type, key); if(!p) throw new Error('Profile unavailable'); return presetJsonText(p, state);}});
      if(result.files.length) downloadArchive({name: 'selected-profiles.zip', data: zipSync(Object.fromEntries(result.files.map(f => [f.filename, strToU8(f.text)])))});
      if(result.failed.length) setBulkError(result.failed.map(f => `${f.name}: ${f.error}`).join('\n'));
    } catch(e) {setBulkError(e instanceof Error ? e.message : String(e));} finally {setBusy(false);}
  };
  const deleteSelected = async (keys?:string[]) => {
    setBusy(true); setBulkError(null);
    try {await archiveRecords(type,keys ?? selectedVisible(latestSelection.current));}
    catch(error) {setBulkError(error instanceof Error ? error.message : String(error));}
    finally {setBusy(false);}
  };
  const [masterLoad,setMasterLoad] = useState<{key:string;error?:string}|null>(null);
  const openMaster = async (baseKey:string) => {
    const intent=selectionRevision.current;
    setMasterLoad({key:baseKey}); setBusy(true);
    try {
      const key=await ensureEditableMaster(type,baseKey);
      if(latestSelection.current.scope === scope && selectionRevision.current === intent)setSelection(reduceSelection(createSelection(scope,[key]),{type:'CLICK',key}));
      setMasterLoad(null);
    } catch(error) {setMasterLoad({key:baseKey,error:error instanceof Error ? error.message : String(error)});}
    finally {setBusy(false);}
  };
  useEffect(()=>{if(masters && !adding && item && !item.user && masterLoad?.key !== item.key) void openMaster(item.key);},[masters,adding,item?.key]);
  const staged=useRef<string|null>(null);
  useEffect(()=>{
    if(!stage || staged.current === stage.id)return;
    const s=getState(),resolved=resolvePreset(s,stage.type,stage.key);
    if(!resolved.source || resolved.error)return;
    staged.current=stage.id;
    updateDraft(JSON.stringify([stage.type,stage.key]),{values:{...resolved.config,...stage.deltas},baseline:resolved.config,origin:resolved.source,printer:s.printer,applicability:resolved.source.applicability,baselineApplicability:resolved.source.applicability,staged:stage});
  },[stage,users,machines,filaments,processes,updateDraft]);
  const targetRequest=useRef({type:initialType,name});
  useEffect(()=>{
    const prior=targetRequest.current;targetRequest.current={type:initialType,name};
    if(prior.type === initialType && prior.name === name || initialType === type && (name === item?.key || name === item?.name))return;
    const p=name ? findProfile(getState(),initialType,name) : undefined;
    if(p)setMasters(p.kind === 'master');
    setAdding(false);pickType(initialType,name);
  },[initialType,name]);
  const compareReason = selected.length !== 2 ? 'Select exactly two visible profiles to compare' : undefined;
  const deleteReason = !selected.length ? 'Select profiles to remove' : undefined;
  return <Modal className="np-dialog pf-window" label="Profiles" onClose={close} backdropClose onEscapeKeyDown={event=>{if(busy || selected.length){event.preventDefault();if(!busy)dispatch({type:'ESCAPE'});}}}>
    <header className="pf-workspace-title"><Dialog.Title asChild><h1>Profiles</h1></Dialog.Title>{recovery.state!=='idle' && recovery.state!=='complete' && adding && <button type="button" className="btn" onClick={()=>setAdding(false)}>Recover saved profiles</button>}<button type="button" className="btn pf-filter-toggle" aria-expanded={filtersOpen} onClick={()=>setFiltersOpen(!filtersOpen)}>Filters{filtersActive ? ` · ${filtersActive}` : ''}</button><SettingsDetail/><button type="button" className="btn-ghost" aria-label="Close profiles" onClick={close}>×</button></header>
    <ProfileLibraryNotice/>
    <div className="np-dialog-body">
      {recovery.state!=='idle' && recovery.state!=='complete' && !adding?<ServiceProfileRecoveryPanel recovery={recovery} onBrowse={()=>{setAdding(true);setAddSource('template');}}/>:<div className={`pf-split${filtersOpen ? ' filters-open' : ''}`} ref={split} style={{'--profile-list-width': `${listWidth}px`} as React.CSSProperties} inert={busy}>
      <aside className="pf-facets" aria-label="Profile filters">
        <fieldset className="pf-facet-group"><legend>Collection</legend><RadioGroup.Root className="pf-radio-list" aria-label="Profile collection" value={type} onValueChange={v=>void leave(()=>{setDrafts({});clearFilters();pickType(v as PresetType);})}>{TYPES.map(([id,label])=><label key={id} className="pf-facet-choice"><Radio value={id}/><span>{label}</span></label>)}</RadioGroup.Root></fieldset>
        <fieldset className="pf-facet-group"><legend>Library</legend><RadioGroup.Root className="pf-radio-list" aria-label="Library" value={masters ? 'masters' : 'owned'} onValueChange={value=>{setMasters(value === 'masters');setAdding(false);setSearch('');}}>{[{id:'owned',label:'My profiles'},{id:'masters',label:'Master profiles'}].map(o=><label key={o.id} className="pf-facet-choice"><Radio value={o.id}/><span>{o.label}</span></label>)}</RadioGroup.Root></fieldset>
        <ProfileFilterGroup label="Brand" options={facets.vendors.map(value=>({value,label:value}))} value={vendorFilter} onChange={setVendorFilter}/>
        {type !== 'filament' && <ProfileFilterGroup label="Printer model" options={facets.models.map(value=>({value,label:value}))} value={modelFilter} onChange={setModelFilter}/>}
        {type !== 'filament' && <ProfileFilterGroup label="Nozzle" options={facets.nozzles.map(value=>({value,label:`${value} mm`}))} value={nozzleFilter} onChange={setNozzleFilter}/>}
        {type === 'filament' && <ProfileFilterGroup label="Material type" options={facets.materials.map(value=>({value,label:value}))} value={materialFilter} onChange={setMaterialFilter}/>}
        {type !== 'machine' && <ProfileFilterGroup label={type === 'filament' ? 'Works with printer' : 'Printer'} options={printerOptions} value={printerFilter} onChange={setPrinterFilter}/>}
        {filtersActive > 0 && <button type="button" className="btn-ghost" onClick={clearFilters}>Clear all filters</button>}
      </aside>
      <section className="pf-my-profiles" aria-label="Profile results">
        <div className="pf-results-heading"><strong>{adding ? 'Add '+(type === 'machine' ? 'printer' : type === 'filament' ? 'filament' : 'print settings') : masters ? 'Master profiles' : 'My profiles'}</strong><span>{visible.length} results</span></div>
        <div className="pf-results-tools"><input className="text pf-profile-search" aria-label="Search profiles" placeholder="Search profiles" value={search} onChange={e=>setSearch(e.target.value)}/><button type="button" className="btn" onClick={()=>{setAdding(!adding);setAddSource('template');setSearch('');}}> {adding ? 'Back' : type === 'machine' ? 'Add printer' : type === 'filament' ? 'Add filament' : 'Add print settings'}</button><MenuButton label="Collection actions" className="btn-ghost" items={[{label:'Import Orca / Bambu',action:()=>void leave(()=>setState({presetUx:{kind:'import',back:{kind:'presets',type,name:item?.key}}}))},{label:'Export profiles',action:()=>void leave(()=>setState({presetUx:{kind:'export'}}))}]}>More</MenuButton></div>
        {adding && <><Segmented label="Add profile from" value={addSource} options={[{id:'template',label:'Catalog template'},{id:'copy',label:'Copy existing'}]} onChange={v=>{setAddSource(v);setSearch('');}}/>{type === 'machine' && <button type="button" className="btn-ghost" onClick={()=>setState({appDialog:'createPrinter'})}>New printer…</button>}</>}
        {selected.length > 1 && <div className="pf-selection-bar"><span>{selected.length} selected</span><button type="button" className="btn-ghost" onClick={() => dispatch({type: 'CLEAR'})}>Clear</button></div>}
        {!adding && !editingKey && selected.length > 1 && <div className="pf-bulk-actions"><button type="button" className="btn" disabled={!selected.length || busy} title={!selected.length ? 'Select visible profiles to export' : 'Export selected profiles'} onClick={() => void exportSelected()}>Export selected</button><button type="button" className="btn" disabled={!!compareReason || busy} title={compareReason ?? 'Compare selected profiles'} onClick={() => {setAdding(false); setComparing(true);}}>Compare</button><button type="button" className="btn-ghost pf-delete" disabled={!!deleteReason || busy} title={deleteReason ?? 'Delete selected profiles'} onClick={() => void deleteSelected()}>Delete selected</button></div>}
        <div className="pf-scroll pf-list" role="grid" aria-label="Profiles" aria-multiselectable="true" onContextMenu={e=>{const row=(e.target as Element).closest<HTMLElement>('[data-profile-key]');if(!row || adding) return;e.preventDefault();const key=row.dataset.profileKey!;if(!selectedSet.has(key)) dispatch({type:'CLICK',key});setContextMenu({x:e.clientX,y:e.clientY,anchor:row});}} tabIndex={visible.length ? -1 : 0} onKeyDown={e => {
          if(e.target instanceof Element && e.target.closest('input,textarea,[contenteditable=true]')) return;
          const command = e.ctrlKey || e.metaKey;
          if(command && e.key.toLocaleLowerCase() === 'a') {e.preventDefault(); e.stopPropagation(); dispatch({type: 'SELECT_ALL'});}
          else if(['ArrowDown','ArrowUp','Home','End'].includes(e.key)) {e.preventDefault(); e.stopPropagation(); dispatch({type: 'ARROW', direction: e.key === 'Home' ? 'first' : e.key === 'End' ? 'last' : e.key === 'ArrowUp' ? 'previous' : 'next', shift: e.shiftKey, focusOnly: command}, true);}
          else if(e.key === 'Delete' || e.key === 'Backspace') {e.preventDefault(); e.stopPropagation(); if(!adding) void deleteSelected();}
          else if((e.key === 'Enter' || e.key === ' ') && e.target instanceof HTMLElement && e.target.dataset.profileKey) {e.preventDefault(); dispatch({type: 'CLICK', key: e.target.dataset.profileKey, toggle: command || e.key === ' ', shift: e.shiftKey});}
        }}>
          {visible.map(i => <ManagerProfileRow key={i.key} item={i} listId={id} selected={selectedSet.has(i.key)} focused={selection.focus === i.key} dispatch={dispatch}/>)}
          {!visible.length && <p className="pf-profile-empty">{search || filtersActive ? 'No profiles match these filters' : adding ? 'No catalog templates available' : 'Add a profile from the catalog'}</p>}
        </div>
      </section><div className="pf-resizer" role="separator" aria-label="Resize profile list" aria-orientation="vertical" aria-valuemin={240} aria-valuemax={480} aria-valuenow={listWidth} tabIndex={0}
        onPointerDown={e => {e.preventDefault(); e.currentTarget.setPointerCapture(e.pointerId);}}
        onPointerMove={e => {if(e.currentTarget.hasPointerCapture(e.pointerId)) resizeList(e.clientX - (split.current?.getBoundingClientRect().left ?? 0), false);}}
        onPointerUp={e => {if(e.currentTarget.hasPointerCapture(e.pointerId)) {resizeList(e.clientX - (split.current?.getBoundingClientRect().left ?? 0), true); e.currentTarget.releasePointerCapture(e.pointerId);}}}
        onKeyDown={e => {if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)) {e.preventDefault(); resizeList(e.key === 'Home' ? 240 : e.key === 'End' ? 480 : listWidth + (e.key === 'ArrowRight' ? 16 : -16), true);}}}/><div className="pf-detail-pane">{filteredDraft && <div className="pf-filtered-draft" role="status">Editing {filteredDraft.name} outside these filters<button type="button" className="btn-ghost" onClick={()=>{clearFilters();setSearch('');}}>Clear filters</button></div>}{adding ? item ? <AddProfileDetails key={item.key} type={type} item={item} copy={addSource === 'copy'} busy={busy} onAdd={name=>void addSelected(item,name)}/> : <section className="pf-bulk-summary"><strong>Select a catalog template</strong></section>
        : comparing && selectedItems.length === 2 ? <Compare type={type} left={selectedItems[0].key} right={selectedItems[1].key} names={selectedItems.map(i => i.key)} setRight={() => {}} close={() => setComparing(false)}/>
         : item?.user ? <ProfileEditor key={item.key} type={type} name={item.key} draft={drafts[JSON.stringify([type,item.key])]} update={updateDraft} onSavingChange={setBusy} onBack={close} onRemove={() => void deleteSelected([item.key])}/>

        : item && masters ? <section className="pf-bulk-summary" role={masterLoad?.error ? 'alert' : 'status'}><strong>{item.name}</strong><p>{masterLoad?.error ?? 'Opening editable master…'}</p>{masterLoad?.error && <button type="button" className="btn" onClick={()=>void openMaster(item.key)}>Retry</button>}</section>
        : <section className="pf-bulk-summary"><strong>{selected.length ? `${selected.length} profiles selected` : 'Select a profile'}</strong>{selectedItems.map(i => <div key={i.key} className="pf-bulk-name">{i.name}</div>)}</section>}
      </div></div>}

      {contextMenu && <ActionMenu label="Profile actions" at={contextMenu} returnFocus={contextMenu.anchor} onClose={()=>setContextMenu(null)} items={[{label:'Export selected',action:()=>void exportSelected()},{label:'Delete from library',action:()=>void deleteSelected(),shortcut:'Delete'}]}/>}
      {bulkError && <div className="pf-editor-error" role="alert">{bulkError}</div>}
    </div>
  </Modal>;
}
