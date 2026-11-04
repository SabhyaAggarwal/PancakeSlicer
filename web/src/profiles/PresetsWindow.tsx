import { savePreferences, usePreference } from '../saved-preferences';
import { ProfileLibraryNotice } from '../components/ProfileLibraryNotice';
import { useEffect, useId, useRef, useSyncExternalStore } from 'react';
import { createSelection, reduceSelection, selectedVisible, type SelectionEvent } from './selection';
import { serializeSelected } from './bulk';
import { findProfile, matchesProfile, profileKey } from './identity';
import { FacetGroup, FacetsHead } from '../components/Facets';
import { Caret } from '../components/Caret';
import { selectCompatibility } from './compatibility';
import { ManagerProfileRow } from './ManagerProfileRow';
import { profileAccess, profileReferenceSnapshot, subscribeProfileReferences } from './references';
import { changedKeys } from './model';
import { applicabilityKey, worksWithNone } from './WorksWith';
import { archiveProfiles, profileCopyCreation, restoreProfiles } from './transactions';
import { documentGeneration } from '../state/history';
import { MasterPresetDetail, ProfileEditor, type ProfileDraft } from './ProfileEditor';
import { baseMenuItems, confirmArchiveBases, openNewBasedOn } from './bases-ui';
import { baseRoleOf } from './bases';
import { materialRank } from './Pickers';
import { modelLabel, nozzleField, printerLine, profileColour, profileSummary, rowDetail, vendorField, vendorLabel } from './metadata';
import { Check } from '../components/Check';
import { Dropdown } from '../components/Dropdown';
import { requestPreset } from '../components/PresetCombo';
import { ActionMenu, MenuButton } from '../components/Menu';
import { Choice, Segmented } from '../components/kit';
import './presets-window.css';
import { useContainerTier } from '../breakpoints';
import { Glyph } from '../components/Glyph';
import { useCallback, useMemo, useState } from 'react';
import type { Config, ConfigValue, PresetType, SystemPreset, UserPreset } from '../api/types';
import { accessSnapshot } from '../api/access';
import { OPTIONS } from '../options';
import { getState, resolvePreset, setState, toast, useStore, type State } from '../state/store';
import { Modal } from '../components/Modal';
import { useMachine } from '../flow/fsm';
import { windowOps, type CopyRequest, type WindowOp } from '../flow/profiles-window-ops';
import { SettingsDetail } from '../components/VisibilityControl';
import { Dialog } from 'radix-ui';
import type { ProjectProfileChanges } from './project-draft';
import { confirmDialog, promptDialog } from '../components/Ask';
import { diffRows, useTabLoaded } from '../components/PresetDialogs';
import { display, presetLabel } from './format';
import { changeCount, takeValue } from './stack';
import { createProfileFromBase, displayName, listCatalog, resolveCatalogEntry } from './catalog';
import { colourName, configColour } from './filament-colour';
import { ColorPicker } from '../components/ColorPicker';

const TYPES: [PresetType, string][] = [['machine', 'Printers'], ['filament', 'Filaments'], ['process', 'Print settings']];
const shortName = presetLabel;
const first = (value: unknown) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '');
const coverOf = (vendor: string, model: string) => vendor && model ? `/profiles/${encodeURIComponent(vendor)}/${encodeURIComponent(model)}_cover.png` : undefined;

interface Item { base?:SystemPreset; key: string; name: string; mine: boolean; flag?: string; parent?: string; changed: number; slot?: number; user?: UserPreset; note: string; vendor: string; model:string; nozzle: string; material: string; family?: string; cover?: string|null; swatch?:string|null;reason?: string; meta?: string; fullName?: string; child?: boolean; config: Config; variants?: Item[] }
const COMMON_MATERIALS = ['PLA', 'PETG', 'ABS', 'ASA', 'TPU'];
const NOUNS: Record<PresetType,[string,string]> = {machine:['printer','printers'],filament:['filament','filaments'],process:['print setting','print settings']};
const count = (n:number,[one,many]:[string,string]) => `${n} ${n === 1 ? one : many}`;
interface Group { key: string; label: string; sub?: string; rows: Item[]; children?: Group[] }
const groupHas = (g: Group, match: (i: Item) => boolean): boolean => g.children ? g.children.some((c) => groupHas(c, match)) : g.rows.some(match);
const byLabel = (a: string, b: string) => a === 'Other' || a === 'Any printer' ? 1 : b === 'Other' || b === 'Any printer' ? -1 : parseFloat(a) - parseFloat(b) || a.localeCompare(b);
const sessionFolds = new Map<string, boolean>();
const unique = (rows: Item[]) => [...new Map(rows.map(r => [r.key, r])).values()];
function families(type: PresetType, list: Item[]): Item[] {
  if(type === 'process') return list;
  const byFamily = new Map<string, Item[]>(), out: Item[] = [];
  for(const i of list) {
    const id = type === 'machine' ? i.model && `${i.vendor}\0${i.model}` : i.family && `${i.family}\0${shortName(i.name)}`;
    if(!id) {out.push(i); continue;}
    if(!byFamily.has(id)) byFamily.set(id, []);
    byFamily.get(id)!.push(i);
  }
  for(const [id, variants] of byFamily) {
    if(type === 'filament' && variants.length === 1) {out.push(variants[0]); continue;}
    variants.sort((a, b) => type === 'machine' ? Number(a.nozzle) - Number(b.nozzle) : a.name.localeCompare(b.name));
    const head = variants[0];
    out.push({...head, key: `family:${type}:${id}`, name: type === 'machine' ? head.model : shortName(head.name), note: '', meta: type === 'machine' ? `${variants.map(v => v.nozzle).filter(Boolean).join(' · ')} mm` : count(variants.length, ['variant', 'variants']), reason: variants.some(v => !v.reason) ? undefined : head.reason, variants});
  }
  return out;
}
function defaultVariant(type: PresetType, variants: Item[]) {
  if(type === 'machine') return [...variants].sort((a, b) => Math.abs(Number(a.nozzle) - 0.4) - Math.abs(Number(b.nozzle) - 0.4))[0];
  return variants.find(v => !v.reason) ?? variants[0];
}

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
    return { key: user ? profileKey(user) : `base:${type}:${name}`, name, mine: isMine, parent, user, base: resolved.base, config:resolved.config,flag:user && user.kind !== 'master' && type !== 'machine' && worksWithNone(type, user.applicability) ? 'Works with no printer' : undefined,swatch:type === 'filament' ? profileColour(resolved.config) : undefined,vendor:type === 'filament' ? first(resolved.config.filament_vendor) || vendor : vendor, model, nozzle:first(resolved.config.nozzle_diameter), material:first(resolved.config.filament_type), cover:type === 'machine' ? coverOf(vendor,model) ?? null : undefined, slot: slots[0], note: noteOf(s, type, identity), changed: slots.reduce((n, i) => n + changeCount(s, type, i), 0) };
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
  const bases=await confirmArchiveBases(type,rows.map(profileKey));
  if(bases !== true)return;
  if(!await confirmDialog('Remove from library',`Remove ${rows.length === 1 ? rows[0].name : `${rows.length} profiles`} from the library?\n${rows.length > 1 ? rows.map(p=>p.name).join('\n')+'\n' : ''}Saved project settings and references are retained. Undo is available.`,['Remove','Cancel']))return;
  const undo=await archiveProfiles(type,rows.map(profileKey));
  toast(`Removed ${rows.length} profile${rows.length === 1 ? '' : 's'} from library`,{label:'Undo',run:()=>void restoreProfiles(undo).then(()=>toast('Profiles restored')).catch(error=>failed('Undo',error))});
}
export async function removePreset(type: PresetType, profile: UserPreset) {try {await archiveRecords(type,[profileKey(profile)]);} catch(error) {failed('Remove',error);}}

function catalogItems(s: State, type: PresetType): Item[] {
  return listCatalog(s,type,{browseAll:true}).map(entry => {
    const {base}=resolveCatalogEntry(s,type,entry.key), config=base.config;
    return {key:entry.key,base,name:entry.name,mine:false,changed:0,note:profileSummary(type,config),vendor:type === 'filament' ? first(config.filament_vendor) || entry.vendor : entry.vendor,model:first(config.printer_model) || base?.model || '',nozzle:first(config.nozzle_diameter),material:first(config.filament_type),family:type === 'filament' ? base.filament_id || base.inherits?.[0] || undefined : undefined,cover:type === 'machine' ? coverOf(entry.vendor,first(config.printer_model) || base?.model || '') ?? null : undefined,swatch:type === 'filament' ? profileColour(config) : undefined,reason:entry.compatible.ok ? undefined : entry.compatible.reason,config};
  });
}
function AddDetails({type,item,busy,onAdd}: {type:PresetType;item:Item;busy:boolean;onAdd(variant:Item,colour?:string):void}) {
  const load=useStore(s=>s.profileLibraryLoad.state);
  const variants=item.variants ?? [item];
  const [choice,setChoice]=useState(()=>defaultVariant(type,variants).key),[colour,setColour]=useState<string>();
  const variant=variants.find(v=>v.key === choice) ?? variants[0];
  const label=busy ? 'Adding…' : load === 'loading' ? 'Loading…' : `Add ${type === 'process' ? NOUNS[type][1] : NOUNS[type][0]}`;
  return <MasterPresetDetail key={variant.key} type={type} preset={variant.key} name={variant.fullName ?? variant.name} config={variant.config}
    identity={type === 'filament' && <ColorPicker value={colour ?? configColour(variant.config) ?? ''} label="Colour" onCommit={setColour}/>}
    controls={<>
      {variants.length > 1 && (type === 'machine'
        ? <Choice label="Nozzle" value={variant.key} options={variants.map(v=>({id:v.key,label:`${v.nozzle} mm`}))} onChange={setChoice}/>
        : <Dropdown className="combo" aria-label="Variant" value={variant.key} onValueChange={setChoice} options={variants.map(v=>({value:v.key,label:v.name}))}/>)}
      <button type="button" className="btn primary" disabled={busy || load !== 'ready'} onClick={()=>onAdd(variant,colour)}>{label}</button>
    </>}/>;
}

function configOf(s: State, type: PresetType, name: string) {
  const slot = slotsOf(s, type, name)[0];
  if (slot === undefined) return resolvePreset(s, type, name).config;
  const { config } = resolvePreset(s, type, name);
  const edits = type === 'filament' ? s.filamentEdits[name] : s.dirty[type];
  return { ...config, ...edits };
}

function Compare({ type, names, titles, close }: { type: PresetType; names: string[]; titles: string[]; close(): void }) {
  const ready = useTabLoaded(type);
  const [only, setOnly] = useState(true);
  useStore((s) => s.dirty);
  useStore((s) => s.filamentEdits);
  const s = getState();
  const useSlot = names.length === 2 ? slotsOf(s, type, names[0])[0] : undefined;
  const rows = useMemo(() => (ready ? diffRows(type, names.map((n) => configOf(s, type, n)), !only) : []), [ready, type, names.join('\n'), only, s.dirty, s.filamentEdits]);
  const changed = rows.filter((r) => r.differs).length;
  const value = (r: (typeof rows)[number], i: number) => (r.values[i] ?? OPTIONS[r.key]?.default) as ConfigValue;
  let group = '';
  return (
    <fieldset className="pf-box pf-compare">
      <legend>Compare</legend>
      <div className="pf-compare-bar">
        <label className="row"><Check checked={only} onCheckedChange={(next) => setOnly(next)} />Only differences</label>
        <span className="pf-dim">{changed} different</span>
        <span className="spacer" />
        <button type="button" className="pf-link" onClick={close}>Close</button>
      </div>
      <div className="pf-scroll pf-diff">
        {!ready ? <div role="status">Loading…</div> : !rows.length ? <div className="pf-dim">No differences</div> : (
          <table className="pf-table">
            <thead><tr><th>Setting</th>{names.map((n, i) => <th key={n} className="n">{titles[i]}</th>)}{useSlot !== undefined && <th aria-label="Use" />}</tr></thead>
            <tbody>{rows.flatMap((r) => {
              const out = [];
              if (r.group !== group) { group = r.group; out.push(<tr key={`g:${group}`} className="pf-group"><td colSpan={names.length + 2}>{group}</td></tr>); }
              out.push(
                <tr key={r.key} className={r.differs ? 'is-diff' : undefined}>
                  <td>{r.label}</td>
                  {names.map((n, i) => <td key={n} className="n">{display(r.key, value(r, i))}</td>)}
                  {useSlot !== undefined && <td>{r.differs && <button type="button" className="pf-link" onClick={() => takeValue(type, r.key, value(r, 1), useSlot)}>← Use</button>}</td>}
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

export default function PresetsWindow({ type: initialType, name, compare, edit = false,add = false,stage,request }: { type: PresetType; name?: string; compare?: string; edit?: boolean;add?:boolean;stage?:ProjectProfileChanges;request?:object }) {
  const libraryReady=useStore(s=>s.profileLibraryLoad.state==='ready');
  const [mobilePane,setMobilePane]=useState<'list'|'detail'|'filters'>(edit || stage || compare!==undefined?'detail':'list');
  const mobileBack=useRef<HTMLButtonElement>(null),mounted=useRef(true);
  const [paneTier,body]=useContainerTier(),drill=paneTier==='narrow' || paneTier==='phone';
  useEffect(()=>{mounted.current=true;return()=>{mounted.current=false;};},[]);
  const pickerAdd=useRef<PresetType|null>(add ? initialType : null);
  useEffect(()=>{if(!request)return;setAdding(!!add);pickerAdd.current=add ? initialType : null;},[request]);
  useEffect(()=>{if(drill && mobilePane!=='list')mobileBack.current?.focus();},[mobilePane]);
  const [type, setType] = useState(initialType), [search, setSearch] = useState(''), [adding, setAdding] = useState(add);
  const [vendorFilter,setVendorFilter] = useState<string[]>([]), [nozzleFilter,setNozzleFilter] = useState<string[]>([]), [materialFilter,setMaterialFilter] = useState<string[]>([]), [modelFilter,setModelFilter]=useState<string[]>([]), [printerFilter,setPrinterFilter]=useState<string[]>([]), [baseFilter,setBaseFilter]=useState<string[]>([]);
  const [filtersOpen,setFiltersOpen]=useState(false);
  const resultsPane = useRef<HTMLElement>(null);
  const drag = useRef<{id:number;x:number;width:number}|null>(null);
  const savedListWidth = usePreference('orca.profileListWidth');
  const minListWidth=300,maxListWidth=440,clampList=(width:number)=>Math.round(Math.max(minListWidth,Math.min(maxListWidth,width)));
  const listWidth = Number(savedListWidth) ? clampList(Number(savedListWidth)) : null;
  const [previewWidth,setPreviewWidth]=useState<number|null>(null);
  const renderedListWidth=previewWidth ?? listWidth;
  const shownListWidth=renderedListWidth ?? clampList(Math.min(1680,innerWidth-32)*.34);
  const resizeList = (width: number, commit: boolean) => {
    const next = clampList(width);
    setPreviewWidth(commit ? null : next);
    if(commit && next!==listWidth)savePreferences({'orca.profileListWidth':next});
  };
  const [drafts, setDrafts] = useState<Record<string, ProfileDraft>>({});
  const updateDraft = useCallback((key: string, draft: ProfileDraft | null) => setDrafts(prior => {const next = {...prior}; if(draft && (Object.keys(changedKeys(draft.values, draft.baseline)).length || applicabilityKey(draft.applicability) !== applicabilityKey(draft.baselineApplicability) || draft.staged)) next[key] = draft; else delete next[key]; return next;}), []);
  const users = useStore(st => st.userPresets), dirty = useStore(st => st.dirty), filamentEdits = useStore(st => st.filamentEdits);
  const machines = useStore(st => st.machines), processes = useStore(st => st.processes), filaments = useStore(st => st.filaments);
  const printer = useStore(st => st.printer), process = useStore(st => st.process), slots = useStore(st => st.filamentSlots);
  const printerOptions=useMemo(()=>users.filter(p=>p.type === 'machine' && !p.archived && p.kind !== 'master').map(p=>({value:profileKey(p),label:p.name})),[users]);
  const [reveal,setReveal]=useState<{key?:string}>({});
  const list = useMemo(() => {
    const s = getState();
    return adding ? families(type,catalogItems(s,type)) : items(s,type);
  }, [adding,type,users,dirty,filamentEdits,machines,processes,filaments,printer,process,slots]);
  const [materialDefault,setMaterialDefault]=useState(false);
  const facets=useMemo(()=>{
    const flat=list.flatMap(i=>i.variants ?? [i]);
    const rows=flat.filter(i=>!vendorFilter.length || vendorFilter.includes(i.vendor));
    const contexts=machines.filter(p=>!modelFilter.length || modelFilter.includes(first(p.config.printer_model) || p.model || ''));
    const unique=(values:string[])=>[...new Set(values.filter(Boolean))].sort();
    return {vendors:unique(flat.map(i=>i.vendor)),models:unique(type === 'machine' ? rows.map(i=>i.model) : machines.map(p=>first(p.config.printer_model) || p.model || '')),nozzles:unique(type === 'machine' ? rows.filter(i=>!modelFilter.length || modelFilter.includes(i.model)).map(i=>i.nozzle) : contexts.map(p=>first(p.config.nozzle_diameter))),materials:unique(rows.map(i=>i.material))};
  },[list,type,machines,vendorFilter,modelFilter]);
  useEffect(()=>{setNozzleFilter(prior=>{const next=prior.filter(n=>facets.nozzles.includes(n));return next.length === prior.length ? prior : next;});},[facets.nozzles]);
  const matching=useMemo(()=>{
    const state=getState();
    const hasContext=type !== 'machine' && (printerFilter.length || modelFilter.length || nozzleFilter.length);
    const contexts=hasContext ? printerFilter.length ? printerFilter : machines.filter(p=>(!modelFilter.length || modelFilter.includes(first(p.config.printer_model) || p.model || '')) && (!nozzleFilter.length || nozzleFilter.includes(first(p.config.nozzle_diameter)))).map(p=>p.name) : [];
    const materials=search.trim() && materialDefault ? [] : materialFilter;
    const out=list.filter(i=>{const variants=i.variants ?? [i];return (!baseFilter.length || baseFilter.some(f=>f === 'base' ? !!baseRoleOf(i.user) : !!i.user?.parentId)) && (!vendorFilter.length || vendorFilter.includes(i.vendor)) && (!materials.length || materials.includes(i.material)) && (type === 'machine' ? (!modelFilter.length || modelFilter.includes(i.model)) && (!nozzleFilter.length || variants.some(v=>nozzleFilter.includes(v.nozzle))) : !hasContext || variants.some(v=>contexts.some(key=>selectCompatibility(state,type,v.user ? v.key : v.name,key).ok)));});
    const pinned=reveal.key && !out.some(i=>i.key === reveal.key) ? list.find(i=>i.key === reveal.key) : undefined;
    return pinned ? [...out,pinned] : out;
  },[list,type,baseFilter,vendorFilter,materialFilter,materialDefault,search,modelFilter,nozzleFilter,printerFilter,machines,users,printer,process,dirty,filamentEdits,reveal.key]);
  const query=search.trim().toLocaleLowerCase();
  const compatibleModels=(i:Item)=>{
    const found=new Map<string,{label:string;sub:string;nozzles:Set<string>}>();
    const ruled=(i.user?.applicability as {printers?:{models?:string[]}} | undefined)?.printers?.models;
    for(const name of ruled ? machines.filter(m=>ruled.includes(first(m.config.printer_model) || m.model || '')).map(m=>m.name) : i.base?.compatible_printers ?? []){
      const m=machines.find(p=>p.name === name);if(!m)continue;
      const full=first(m.config.printer_model) || m.model || '';if(!full)continue;
      const short=modelLabel(m.config,m.vendor,m.model),entry=found.get(full) ?? {label:short,sub:short === full ? '' : vendorLabel(m.config,m.vendor),nozzles:new Set<string>()};
      entry.nozzles.add(first(m.config.nozzle_diameter));found.set(full,entry);
    }
    return found;
  };
  const machineRow=(i:Item):Item=>i.variants ? {...i,fullName:i.model,name:modelLabel(i.config,i.vendor,i.model)}
    : i.user?.kind === 'master' ? {...i,fullName:i.name,name:printerLine(i.config,i.vendor,i.model),note:''}
    : {...i,note:rowDetail(i.name,[{value:modelLabel(i.config,i.vendor,i.model)},nozzleField(first(i.config.nozzle_diameter))],[vendorLabel(i.config,i.vendor)])};
  const filamentRow=(i:Item,group:string):Item=>{
    if(i.variants)return i;
    const at=/\s*@.*$/.exec(i.name),scoped=printerFilter.length === 1 && printerFilter[0] === printer,models=[...compatibleModels(i).values()].map(m=>m.label);
    const title=at && (scoped || models.length) ? i.name.slice(0,at.index) : i.name;
    const tag=at && !scoped && models.length ? models[0]+(models.length > 1 ? ` +${models.length-1}` : '') : '';
    return {...i,fullName:title === i.name ? undefined : i.name,name:title,note:rowDetail(title,[{value:i.material},vendorField(i.vendor)],[group]),meta:[i.meta,tag].filter(Boolean).join(' · ') || undefined};
  };
  const colourFamilies=(list:Item[])=>{
    const keys=new Set(list.map(i=>i.key)),parentOf=(i:Item)=>i.user?.parentId && keys.has(i.user.parentId) ? i.user.parentId : undefined;
    return list.filter(i=>!parentOf(i)).flatMap(parent=>[parent,...list.filter(i=>parentOf(i) === parent.key).map(i=>({...i,child:true}))]);
  };
  const groups=useMemo(():Group[]=>{
    const found=matching.filter(i=>!query || [i,...i.variants ?? []].some(v=>`${v.name} ${v.note}`.toLocaleLowerCase().includes(query)));
    const rows=type === 'process' ? found : found.filter(i=>!i.flag),stray=type === 'process' ? [] : found.filter(i=>i.flag).map(i=>({...i,flag:undefined}));
    const strayGroup:Group[]=stray.length ? [{key:'No printer',label:'No printer',rows:stray.map(i=>type === 'filament' ? filamentRow(i,i.material || 'Other') : i).sort((a,b)=>a.name.localeCompare(b.name))}] : [];
    const bucket=(list:Item[],labelOf:(i:Item)=>string)=>{const m=new Map<string,Item[]>();for(const i of list){const g=labelOf(i);m.set(g,[...m.get(g) ?? [],i]);}return [...m].sort(([a],[b])=>byLabel(a,b));};
    const sortRows=(list:Item[])=>list.sort((a,b)=>a.name.localeCompare(b.name));
    if(type !== 'process')return [...bucket(rows,i=>type === 'machine' ? vendorLabel(i.config,i.vendor) : i.material || 'Other').sort(([a],[b])=>type === 'filament' ? materialRank(a)-materialRank(b) || byLabel(a,b) : 0)
      .map(([label,list])=>({key:label,label,rows:type === 'machine' ? sortRows(list.map(machineRow)) : type === 'filament' ? (adding ? sortRows(list.map(i=>filamentRow(i,label))) : colourFamilies(sortRows(list.map(i=>filamentRow(i,label))))) : sortRows(list)})),...strayGroup];
    const speeds:Record<string,string>={quality:'Quality',balanced:'Balanced',fast:'Fast'};
    const mine=rows.filter(i=>i.user && i.user.kind !== 'master').map(i=>({...i,fullName:i.name,note:'',meta:speeds[(i.user as {auto?:{preferredSpeed?:string}}).auto?.preferredSpeed ?? '']}));
    return [{key:'mine',label:'My profiles',rows:colourFamilies(sortRows(mine))}];
  },[matching,query,type,machines,printerFilter,printer]);
  const visible=useMemo(()=>unique(groups.flatMap(g=>g.rows)),[groups]);
  const [folds,setFolds]=useState<ReadonlyMap<string,boolean>>(()=>new Map(sessionFolds)),[searchFolds,setSearchFolds]=useState<ReadonlySet<string>>(new Set());
  useEffect(()=>setSearchFolds(new Set()),[query]);
  const setFold=(key:string,open:boolean)=>{sessionFolds.set(key,open);setFolds(prior=>new Map(prior).set(key,open));};
  const filtersActive=baseFilter.length+vendorFilter.length+modelFilter.length+nozzleFilter.length+materialFilter.length+printerFilter.length;
  const clearFilters=()=>{setBaseFilter([]);setVendorFilter([]);setModelFilter([]);setNozzleFilter([]);setMaterialFilter([]);setPrinterFilter([]);setMaterialDefault(false);};
  const defaulted=useRef('');
  useEffect(()=>{
    if(!adding){defaulted.current='';return;}
    const view=`${type}`;
    if(defaulted.current === view || !list.length)return;
    defaulted.current=view;
    if(type !== 'machine' && printerOptions.some(o=>o.value === printer))setPrinterFilter([printer]);
    const common=type === 'filament' ? COMMON_MATERIALS.filter(m=>facets.materials.includes(m)) : [];
    if(common.length){setMaterialFilter(common);setMaterialDefault(true);}
  },[adding,type,list,printerOptions,printer]);
  const scope = `profiles:${adding ? 'add' : 'owned'}:${type}`, groupKey = (key: string) => `${scope}:${key}`;
  const printerModel=useMemo(()=>first(resolvePreset(getState(),'machine',printer).config.printer_model),[printer,machines,users]);
  const foldOpen = (g: Group) => folds.get(groupKey(g.key)) ?? (type === 'process' ? adding && (g.key === printerModel || groups.length === 1) : !adding || groupHas(g, r => r.slot !== undefined || r.key === name));
  const isOpen = (g: Group) => query ? !searchFolds.has(groupKey(g.key)) : foldOpen(g);
  const order = useMemo(() => {
    const keys: string[] = [], walk = (list: Group[]) => {for(const g of list)if(isOpen(g)){if(g.children)walk(g.children);else keys.push(...g.rows.map(i => i.key));}};
    walk(groups);
    return [...new Set(keys)];
  }, [groups, folds, searchFolds, query, scope, name, adding]);
  const current = (t: PresetType) => (t === 'machine' ? printer : t === 'process' ? process : slots[0]) ?? '';
  const [selectionState, setSelection] = useState(() => {
    let initial = createSelection(scope, order);
    const target = (name ? list.find(i => i.name === name || i.key === name) : undefined) ?? list.find(i => i.key === current(initialType) || i.name === current(initialType)) ?? list[0];
    if(target) initial = reduceSelection(initial, {type: 'CLICK', key: target.key});
    if(compare !== undefined) {const other = list.find(i => i.name === compare || i.key === compare) ?? list.find(i => i.key !== target?.key); if(other) initial = reduceSelection(initial, {type: 'CLICK', key: other.key, toggle: true});}
    return initial;
  });
  const previousScope = useRef(scope), initializedSelection = useRef(order.length > 0);
  const kept = useMemo(() => reveal.key && !order.includes(reveal.key) && visible.some(i => i.key === reveal.key) ? [...order, reveal.key] : order, [order, reveal.key, visible]);
  const selection = useMemo(() => reduceSelection(selectionState, {type: 'SYNC', scope, visible: kept}), [selectionState, scope, kept]);
  useEffect(() => {
    const changed=previousScope.current !== scope; previousScope.current=scope;
    const initialize = changed || !initializedSelection.current;
    if(order.length) initializedSelection.current=true;
    setSelection(prior=>{
      let next=reduceSelection(prior,{type:'SYNC',scope,visible:kept});
      if(visible.length && next.selected.length === 0 && (initialize || prior.selected.length > 0 && !prior.selected.some(key=>drafts[JSON.stringify([type,key])]))) {
        const selectedKey=current(type), target=visible.find(i=>i.key === name || i.name === name || i.fullName === name) ?? visible.find(i=>i.key === selectedKey || i.name === selectedKey) ?? visible[0];
        next=reduceSelection(next,{type:'CLICK',key:target.key});
        if(!adding)setReveal({key:target.key});
      }
      return next;
    });
  },[scope,kept,visible.length]);
  const selected = selectedVisible(selection), selectedSet = useMemo(() => new Set(selected), [selection]);
  const selectedItems=visible.filter(i=>selectedSet.has(i.key));
  const inspected=useRef<string|null>(null);
  const filteredDraft=inspected.current && !order.includes(inspected.current) && drafts[JSON.stringify([type,inspected.current])] ? list.find(i=>i.key === inspected.current) : undefined;
  const item=filteredDraft ?? (selectedItems.length === 1 ? selectedItems[0] : undefined);
  if(item && !filteredDraft)inspected.current=item.key;
  const latestSelection = useRef(selection); latestSelection.current = selection;
  const selectionRevision=useRef(0);
  const id = useId();
  void edit;
  const [compareHidden, setCompareHidden] = useState<string | null>(null);
  const ops = useMachine(windowOps), busy = ops.value !== 'idle', pendingCopy = ops.context.copy;
  const run = async (op: WindowOp, task: () => Promise<void>) => {
    if(!windowOps.can({type: 'BEGIN', op})) return;
    windowOps.send({type: 'BEGIN', op});
    try {await task();} finally {windowOps.send({type: 'END', op});}
  };
  const editorSaving = useCallback((saving: boolean) => {
    if(saving) {if(windowOps.can({type: 'BEGIN', op: 'editor-saving'})) windowOps.send({type: 'BEGIN', op: 'editor-saving'});}
    else if(windowOps.get().value === 'editor-saving') windowOps.send({type: 'END', op: 'editor-saving'});
  }, []);
  const [bulkError, setBulkError] = useState<string | null>(null);
  const [contextMenu,setContextMenu] = useState<{x:number;y:number;anchor:HTMLElement}|null>(null);
  const references = useSyncExternalStore(subscribeProfileReferences, profileReferenceSnapshot);
  const fallbackKey=useRef<()=>string|undefined>(()=>undefined);
  fallbackKey.current=()=>{if(adding)return undefined;const used=current(type);return (visible.find(i=>i.key === used || i.name === used) ?? visible[0])?.key;};
  const dispatch = useCallback((event: SelectionEvent, focus = false) => {
    selectionRevision.current++;initializedSelection.current=true;inspected.current=null;
    let next = reduceSelection(latestSelection.current, event);
    const fallback = next.selected.length ? undefined : fallbackKey.current();
    if(fallback) {next = reduceSelection(next, {type: 'CLICK', key: fallback}); setReveal({key: fallback});}
    latestSelection.current = next; setSelection(next);
    if(event.type==='CLICK' && !event.toggle && !event.shift)setMobilePane('detail');
    if(focus && next.focus) requestAnimationFrame(() => {const row = document.getElementById(`${id}:${next.focus}`); row?.focus({preventScroll: true}); row?.scrollIntoView({block: 'nearest'});});
  }, [id]);
  const openRow=adding ? undefined : item?.key;
  useEffect(() => {if(openRow !== reveal.key)setReveal({key: openRow});}, [openRow]);
  const clickedIn=useRef<string|undefined>(undefined), [openIn,setOpenIn]=useState<string>(), revealed=useRef<string|undefined>(undefined);
  const revealPath=(key:string)=>{
    const paths:Group[][]=[],walk=(list:Group[],at:Group[])=>{for(const g of list){if(g.children)walk(g.children,[...at,g]);else if(g.rows.some(r=>r.key === key))paths.push([...at,g]);}};
    walk(groups,[]);
    const path=paths.find(p=>p[p.length-1].key === clickedIn.current) ?? paths.find(p=>p.every(foldOpen)) ?? paths.find(p=>p[0].key === printerModel) ?? paths[0];
    clickedIn.current=undefined;
    if(!path)return false;
    setOpenIn(path[path.length-1].key);
    if(!path.every(foldOpen))setFolds(prior=>{const next=new Map(prior);for(const g of path)next.set(groupKey(g.key),true);return next;});
    return true;
  };
  useEffect(()=>{if(!openRow || query || revealed.current === `${scope}:${openRow}`)return;if(revealPath(openRow))revealed.current=`${scope}:${openRow}`;},[openRow,groups,query,scope]);
  useEffect(() => {const key=item?.key; if(key) requestAnimationFrame(() => document.getElementById(`${id}:${key}`)?.scrollIntoView({block: 'nearest'}));}, [item?.key, type, adding, !query]);
  const [renameRequest,setRenameRequest]=useState<{key:string;at:number}>();
  const requestRename=(key:string)=>{if(adding)return;if(item?.key !== key)dispatch({type:'CLICK',key});setRenameRequest({key,at:Date.now()});};
  const access = useMemo(() => profileAccess(type), [type, references, users]);
  const leave = async (action: () => void) => {if(Object.keys(drafts).length && !await confirmDialog('Unsaved profile edits', 'Discard unsaved profile edits?', ['Discard', 'Keep editing'])) return; action();};
  const close=()=>{if(!busy)void leave(()=>setState({presetUx:null}));};
  const pickType = (t: PresetType, target?: string) => {
    selectionRevision.current++;
    setType(t); setSearch(''); setVendorFilter([]); setNozzleFilter([]); setMaterialFilter([]); setBulkError(null);
    const rows = adding ? [] : items(getState(), t), nextScope = `profiles:${adding ? 'add' : 'owned'}:${t}`;
    let next = createSelection(nextScope, rows.map(i => i.key));
    const row = rows.find(i => i.key === target || i.name === target);
    if(row) next = reduceSelection(next, {type: 'CLICK', key: row.key});
    setSelection(next);
  };
  const added = (n: string,targetType=type) => {setType(targetType); setAdding(false); setMobilePane('detail');setSearch(''); setVendorFilter([]); setNozzleFilter([]); setMaterialFilter([]); const p = findProfile(getState(), targetType, n); if(p) {const key = profileKey(p); const initial = createSelection(`profiles:owned:${targetType}`, [key]); setSelection(reduceSelection(initial,{type: 'CLICK',key})); setReveal({key});}};
  const consumeCopy=async(captured:CopyRequest,owns:()=>boolean)=>{
    const key=await captured.creation.commit();
    if(windowOps.get().context.copy===captured)windowOps.send({type:'COPY_DONE'});
    if(owns())added(key,captured.source.type);else toast(`Added ${captured.name}`);
  };
  const owner=()=>{
    const revision=selectionRevision.current,document=documentGeneration(),before=getState();
    return ()=>mounted.current && documentGeneration()===document && getState().api===before.api && selectionRevision.current===revision
      && getState().printer===before.printer && getState().process===before.process && getState().filamentSlots===before.filamentSlots;
  };
  const addVariant = async (variant:Item,colour?:string) => {
    if(getState().profileLibraryLoad.state!=='ready')return;
    await run('adding', async () => {
    setBulkError(null);
    const owns=owner();
    try {
      const recolour=colour && variant.base && colour !== configColour(variant.config) ? colour : undefined;
      const key=recolour ? await createProfileFromBase(type,variant.key,`${displayName(variant.base!)} ${colourName(recolour)}`,{filament_colour:[recolour]}) : await createProfileFromBase(type,variant.key);
      if(!owns()){toast(`Added ${variant.name}`);return;}
      added(key);
      if(pickerAdd.current === type){pickerAdd.current=null;requestPreset(type,0,key);}
    }
    catch(error) {if(mounted.current)setBulkError(error instanceof Error?error.message:String(error));}
    });
  };
  const retryCopy=async()=>{
    const copy=windowOps.get().context.copy;if(!copy || !libraryReady)return;
    await run('copying', async () => {
      try{await consumeCopy(copy,owner());}
      catch(error){windowOps.send({type:'COPY_FAILED',error:error instanceof Error?error.message:String(error)});}
    });
  };
  const duplicate=async(source:UserPreset,independent=false)=>{
    if(busy || !libraryReady)return;
    if(pendingCopy){if(pendingCopy.source.id===source.id)await retryCopy();else setBulkError(`Finish or discard the copy of ${pendingCopy.source.name} first.`);return;}
    const copyName=await promptDialog(`${independent ? 'Duplicate as independent copy' : 'Duplicate'} ${NOUNS[type][0]}`,`${source.name} copy`);
    if(!copyName || windowOps.get().context.copy || windowOps.get().value !== 'idle')return;
    if(getState().userPresets.some(p=>p.type === source.type && !p.archived && p.name === copyName)){setBulkError(`${copyName} already exists.`);return;}
    windowOps.send({type:'COPY',source,name:copyName,creation:profileCopyCreation(source,copyName,independent)});
    await retryCopy();
  };
  const discardCopy=()=>{if(windowOps.can({type:'DISCARD'}))windowOps.send({type:'DISCARD'});};
  const exportSelected = () => run('exporting', async () => {
    setBulkError(null);
    try {
      const state = getState(), {presetJsonText, downloadArchive} = await import('../preset-io'), {strToU8, zipSync} = await import('fflate');
      const result = serializeSelected(latestSelection.current, {label: access.label, serialize: key => {const p = findProfile(state, type, key); if(!p) throw new Error('Profile unavailable'); return presetJsonText(p, state);}});
      if(result.files.length) downloadArchive({name: 'selected-profiles.zip', data: zipSync(Object.fromEntries(result.files.map(f => [f.filename, strToU8(f.text)])))});
      if(result.failed.length) setBulkError(result.failed.map(f => `${f.name}: ${f.error}`).join('\n'));
    } catch(e) {setBulkError(e instanceof Error ? e.message : String(e));}
  });
  const deleteSelected = (keys?:string[]) => run('deleting', async () => {
    setBulkError(null);
    try {await archiveRecords(type,keys ?? selectedVisible(latestSelection.current));}
    catch(error) {setBulkError(error instanceof Error ? error.message : String(error));}
  });
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
    setAdding(!!add);pickType(initialType,name);
  },[initialType,name]);
  const backToList=()=>{setMobilePane('list');setFiltersOpen(false);requestAnimationFrame(()=>document.getElementById(`${id}:${latestSelection.current.focus}`)?.focus());};
  const nouns=NOUNS[type], typeLabel=type === 'process' ? nouns[1] : nouns[0];
  const resultNoun:[string,string]=adding && type === 'machine' ? ['model','models'] : adding ? ['preset','presets'] : nouns;
  const toggleGroup=(g:Group)=>{const k=groupKey(g.key);if(query)setSearchFolds(prior=>{const next=new Set(prior);if(!next.delete(k))next.add(k);return next;});else setFold(k,!isOpen(g));};
  useEffect(()=>{const key=selection.focus;if(query && key && revealPath(key))revealed.current=`${scope}:${key}`;},[selection.focus]);
  const renderGroup=(g:Group,depth:number):React.ReactNode=>{
    const open=isOpen(g);
    return <div key={g.key} role="rowgroup" className="pf-group" data-depth={depth}>
      <div role="row" className="pf-group-head"><span role="rowheader"><button type="button" className="pf-group-toggle" aria-expanded={open} onClick={()=>toggleGroup(g)}><Caret dir={open ? 'down' : 'right'}/><span className="pf-group-label">{g.label}</span>{g.sub && <span className="pf-dim">{g.sub}</span>}</button></span></div>
      {open && (g.children ? g.children.map(c=>renderGroup(c,depth+1)) : <div role="presentation" className="pf-group-rows" onClickCapture={()=>{clickedIn.current=g.key;setOpenIn(g.key);}}>{g.rows.map(i => <ManagerProfileRow key={i.key} item={i} listId={id} selected={selectedSet.has(i.key)} open={i.key === openKey && (!openIn || openIn === g.key || !groups.some(o=>o.key === openIn && groupHas(o,r=>r.key === i.key)))} focused={selection.focus === i.key} selectable={!adding} dispatch={dispatch}/>)}</div>)}
    </div>;
  };
  const handlers=useRef({close,addVariant,deleteSelected,duplicate});handlers.current={close,addVariant,deleteSelected,duplicate};
  const editKey=!adding && item?.user ? item.key : undefined,editUser=editKey ? item?.user : undefined,draft=editKey ? drafts[JSON.stringify([type,editKey])] : undefined;
  const editor=useMemo(()=>editKey && editUser && <ProfileEditor key={editKey} type={type} name={editKey} draft={draft} update={updateDraft} onSavingChange={editorSaving} onBack={()=>handlers.current.close()} onRemove={() => void handlers.current.deleteSelected([editKey])} onDuplicate={editUser.kind === 'master' ? undefined : () => void handlers.current.duplicate(editUser)} onDuplicateIndependent={editUser.kind === 'master' || type !== 'process' ? undefined : () => void handlers.current.duplicate(editUser,true)} foldActions={drill} renameRequest={renameRequest}/>,[editKey,editUser,type,draft,drill,updateDraft,renameRequest]);
  const addDetails=useMemo(()=>adding && item ? <AddDetails key={item.key} type={type} item={item} busy={busy} onAdd={(variant,colour)=>void handlers.current.addVariant(variant,colour)}/> : null,[adding,item,type,busy]);
  const bulk=selected.length > 1 && !adding, selectionKey=selected.join('\n');
  const canCompare=bulk && selected.length <= 4 && selectedItems.length === selected.length, comparing=canCompare && compareHidden !== selectionKey;
  const openKey=comparing ? undefined : item?.key;
  const bulkActions=<><button type="button" className="btn-ghost" aria-pressed={comparing} disabled={!canCompare || busy} onClick={() => setCompareHidden(comparing ? selectionKey : null)}>Compare</button>
    <button type="button" className="btn-ghost" disabled={busy} onClick={() => void exportSelected()}>Export</button>
    <button type="button" className="btn-ghost pf-delete" disabled={busy} onClick={() => void deleteSelected()}>Delete</button>
    <button type="button" className="icon-btn" aria-label="Clear selection" onClick={() => dispatch({type: 'CLEAR'})}><Glyph name="close" size={14} /></button></>;
  const showLibrary=(master:boolean)=>{if(master === adding || master && type === 'process')return;void leave(()=>{setDrafts({});clearFilters();setSearch('');setAdding(master);setMobilePane('list');});};
  return <Modal className="np-dialog pf-window" label="Profiles" onClose={close} backdropClose onEscapeKeyDown={event=>{if(busy || (event.target as Element | null)?.closest?.('.pf-name-input')){event.preventDefault();return;}if(drill && mobilePane!=='list'){event.preventDefault();backToList();}else if(selected.length > 1){event.preventDefault();dispatch({type:'ESCAPE'});}}}>
    <header className="pf-workspace-title">
      <Dialog.Title asChild><h1>Profiles</h1></Dialog.Title>
      <Segmented label="Profile type" value={type} options={TYPES.map(([id,label])=>({id,label}))} onChange={v=>void leave(()=>{setDrafts({});clearFilters();pickType(v as PresetType);})}/>
      {!drill && <SettingsDetail compact/>}
      <button type="button" className="btn-ghost pf-close" aria-label="Close profiles" onClick={close}><Glyph name="close" size={14} /></button>
    </header>
    <ProfileLibraryNotice/>
    {pendingCopy && <div className="pf-copy-pending" role={pendingCopy.error ? 'alert' : 'status'}>
      <strong>{pendingCopy.name}</strong><span>{pendingCopy.error ?? 'Duplicate not finished'}</span>
      <button type="button" className="btn" disabled={busy || !libraryReady} onClick={()=>void retryCopy()}>Retry</button>
      {!pendingCopy.creation.retryable && <button type="button" className="btn-ghost" disabled={busy} onClick={discardCopy}>Discard</button>}
    </div>}
    <div className="np-dialog-body" ref={body}>
      <div className={`pf-split${filtersOpen ? ' filters-open' : ''}`} data-library={adding ? 'master' : 'mine'} data-type={type} data-mobile-pane={mobilePane} style={renderedListWidth === null ? undefined : {'--profile-list-width': `${renderedListWidth}px`} as React.CSSProperties} inert={busy}>
      <div className="pf-mobile-navigation"><button ref={mobileBack} type="button" className="btn-ghost pf-back" onClick={backToList}><Glyph name="back"/>Back</button>{mobilePane==='filters' ? <strong>Filters</strong> : <SettingsDetail compact/>}</div>
      <aside id={`${id}-filters`} className="pf-facets facets" aria-label="Library and filters">
        <nav className="pf-library" aria-label="Library">
          <button type="button" className="pf-library-item" aria-current={!adding ? 'page' : undefined} onClick={()=>showLibrary(false)}>My {nouns[1]}</button>
          {type !== 'process' && <button type="button" className="pf-library-item" aria-current={adding ? 'page' : undefined} onClick={()=>showLibrary(true)}>Master presets</button>}
        </nav>
        <FacetsHead active={filtersActive} onClear={clearFilters}/>
        {!adding && <FacetGroup label="Bases" options={[...(list.some(i=>baseRoleOf(i.user)) ? [{value:'base',label:'Bases'}] : []),...(list.some(i=>i.user?.parentId) ? [{value:'based',label:'On a base'}] : [])]} value={baseFilter} onChange={setBaseFilter}/>}
        {type === 'filament' && <FacetGroup label="Material" options={[...facets.materials].sort((a,b)=>materialRank(a)-materialRank(b) || a.localeCompare(b)).map(value=>({value,label:value}))} value={materialFilter} onChange={next=>{setMaterialFilter(next);setMaterialDefault(false);}}/>}
        <FacetGroup label="Brand" options={facets.vendors.map(value=>({value,label:type === 'filament' ? value : vendorLabel({},value)}))} value={vendorFilter} onChange={setVendorFilter}/>
        {type !== 'filament' && <FacetGroup label="Printer model" options={facets.models.map(value=>({value,label:value}))} value={modelFilter} onChange={setModelFilter}/>}
        {type !== 'filament' && <FacetGroup label="Nozzle" options={facets.nozzles.map(value=>({value,label:`${value} mm`}))} value={nozzleFilter} onChange={setNozzleFilter}/>}
        {type !== 'machine' && <FacetGroup label={type === 'filament' ? 'Works with' : 'Printer'} options={printerOptions} value={printerFilter} onChange={setPrinterFilter}/>}
        <button type="button" className="btn pf-mobile-show-results" onClick={backToList}>Show {count(visible.length,resultNoun)}</button>
      </aside>
      <section className="pf-my-profiles" ref={resultsPane} aria-label={adding ? 'Master presets' : `My ${nouns[1]}`}>
        <div className="pf-list-head"><div className="pf-results-heading" data-selection={bulk || undefined}>{bulk ? <><strong>{selected.length} selected</strong>{bulkActions}</>
          : <><strong>{count(visible.length,resultNoun)}</strong>
          {adding ? <button type="button" className="btn" onClick={()=>showLibrary(false)}>Cancel</button>
            : type === 'process' ? <button type="button" className="btn" disabled={!item?.user || !libraryReady} onClick={()=>item?.user && void duplicate(item.user)}>New profile</button>
            : <button type="button" className="btn" onClick={()=>showLibrary(true)}>Add {typeLabel}</button>}</>}
        </div>
        <div className="pf-results-tools"><input className="text pf-profile-search" type="search" aria-label="Search" placeholder="Search" value={search} onChange={e=>setSearch(e.target.value)}/>
          <button type="button" hidden={type === 'process'} className="icon-btn pf-filter-toggle" aria-label={filtersActive ? `Filters, ${filtersActive} active` : 'Filters'} aria-expanded={filtersOpen || mobilePane==='filters'} aria-controls={`${id}-filters`} onClick={()=>{setFiltersOpen(!filtersOpen);setMobilePane(mobilePane==='filters'?'list':'filters');}}><Glyph name="filter"/>{filtersActive ? <span className="pf-filter-count" aria-hidden>{filtersActive}</span> : null}</button>
          {adding ? type === 'machine' && <button type="button" className="btn-ghost" onClick={()=>setState({appDialog:'createPrinter'})}>Custom printer</button>
            : <MenuButton label="Import, export or reset profiles" className="icon-btn" items={[{label:'New profile based on…',disabled:!libraryReady,action:()=>void openNewBasedOn(type)},'sep',{label:'Import',action:()=>void leave(()=>setState({presetUx:{kind:'import',back:{kind:'presets',type,name:item?.key}}}))},{label:'Export',action:()=>void leave(()=>setState({presetUx:{kind:'export'}}))}]}><Glyph name="more"/></MenuButton>}
        </div></div>
        <div className="pf-scroll pf-list" data-kind={type} data-multi={bulk || undefined} role="grid" aria-label={adding ? 'Master presets' : `My ${nouns[1]}`} aria-multiselectable={!adding} onDoubleClick={e=>{const row=(e.target as Element).closest<HTMLElement>('[data-profile-key]');if(row && (e.target as Element).closest('.pf-item-name'))requestRename(row.dataset.profileKey!);}} onContextMenu={e=>{const row=(e.target as Element).closest<HTMLElement>('[data-profile-key]');if(!row || adding) return;e.preventDefault();const key=row.dataset.profileKey!;if(!selectedSet.has(key)) dispatch({type:'CLICK',key});setContextMenu({x:e.clientX,y:e.clientY,anchor:row});}} tabIndex={visible.length ? -1 : 0} onKeyDown={e => {
          if(e.target instanceof Element && e.target.closest('input,textarea,[contenteditable=true]')) return;
          const command = e.ctrlKey || e.metaKey;
          if(command && e.key.toLocaleLowerCase() === 'a' && !adding) {e.preventDefault(); e.stopPropagation(); dispatch({type: 'SELECT_ALL'});}
          else if(['ArrowDown','ArrowUp','Home','End'].includes(e.key)) {e.preventDefault(); e.stopPropagation(); dispatch({type: 'ARROW', direction: e.key === 'Home' ? 'first' : e.key === 'End' ? 'last' : e.key === 'ArrowUp' ? 'previous' : 'next', shift: !adding && e.shiftKey, focusOnly: command}, true);}
          else if(e.key === 'F2' && e.target instanceof HTMLElement && e.target.dataset.profileKey) {e.preventDefault(); requestRename(e.target.dataset.profileKey);}
          else if(e.key === 'Delete' || e.key === 'Backspace') {e.preventDefault(); e.stopPropagation(); if(!adding) void deleteSelected();}
          else if((e.key === 'Enter' || e.key === ' ') && e.target instanceof HTMLElement && e.target.dataset.profileKey) {e.preventDefault(); dispatch({type: 'CLICK', key: e.target.dataset.profileKey, toggle: !adding && (command || e.key === ' '), shift: !adding && e.shiftKey});}
        }}>
          {groups.map(g => renderGroup(g, 0))}
          {!visible.length && <div className="pf-profile-empty">{search || filtersActive ? <><span>No {resultNoun[1]} match</span><button type="button" className="btn-ghost" onClick={()=>{clearFilters();setSearch('');}}>Clear filters</button></>
            : adding ? <span>No master presets</span> : <><span>No {nouns[1]} yet</span>{type !== 'process' && <button type="button" className="btn" onClick={()=>showLibrary(true)}>Add {typeLabel}</button>}</>}</div>}
        </div>
      </section><div className="pf-resizer" role="separator" aria-label="Resize profile list" aria-orientation="vertical" aria-valuemin={minListWidth} aria-valuemax={maxListWidth} aria-valuenow={shownListWidth} tabIndex={0}
        onPointerDown={e => {if(e.button!==0 || !resultsPane.current)return;e.preventDefault();drag.current={id:e.pointerId,x:e.clientX,width:resultsPane.current.getBoundingClientRect().width};e.currentTarget.setPointerCapture(e.pointerId);}}
        onPointerMove={e => {const start=drag.current;if(start?.id===e.pointerId && e.currentTarget.hasPointerCapture(e.pointerId))resizeList(start.width+e.clientX-start.x,false);}}
        onPointerUp={e => {const start=drag.current;if(start?.id===e.pointerId && e.currentTarget.hasPointerCapture(e.pointerId)){drag.current=null;if(e.currentTarget.getClientRects().length)resizeList(start.width+e.clientX-start.x,true);else setPreviewWidth(null);e.currentTarget.releasePointerCapture(e.pointerId);}}}
        onPointerCancel={e => {drag.current=null;setPreviewWidth(null);if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);}}
        onLostPointerCapture={()=>{drag.current=null;setPreviewWidth(null);}}
        onKeyDown={e => {if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)) {e.preventDefault();resizeList(e.key==='Home'?minListWidth:e.key==='End'?maxListWidth:(resultsPane.current?.getBoundingClientRect().width ?? shownListWidth)+(e.key==='ArrowRight'?16:-16),true);}}}/><div className="pf-detail-pane">{filteredDraft && <div className="pf-filtered-draft" role="status"><span>Hidden by filters</span><button type="button" className="more-toggle" onClick={()=>{clearFilters();setSearch('');}}>Clear filters</button></div>}
        {adding ? addDetails
        : comparing ? <Compare type={type} names={selectedItems.map(i => i.key)} titles={selectedItems.map(i => i.name)} close={() => setCompareHidden(selectionKey)}/>
        : item?.user ? editor
        : bulk ? <section className="pf-bulk-summary"><strong>{count(selected.length,nouns)} selected</strong><div className="pf-bulk-actions">{bulkActions}</div></section> : null}
      </div></div>

      {contextMenu && <ActionMenu label="Profile actions" at={contextMenu} returnFocus={contextMenu.anchor} onClose={()=>setContextMenu(null)} items={[...(selected.length === 1 && selectedItems[0]?.user && selectedItems[0].user.kind !== 'master' ? [{label:'Rename',action:()=>requestRename(selectedItems[0].key),shortcut:'F2'}] : []),...(canCompare && !comparing ? [{label:'Compare',action:()=>setCompareHidden(null)}] : []),...(selected.length === 1 && selectedItems[0]?.user ? baseMenuItems(type,selectedItems[0].key) : []),{label:'Export',action:()=>void exportSelected()},{label:'Delete',action:()=>void deleteSelected(),shortcut:'Delete'}]}/>}
      {bulkError && <div className="pf-editor-error" role="alert">{bulkError}</div>}
    </div>
  </Modal>;
}
