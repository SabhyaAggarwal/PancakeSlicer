import { Component, useEffect, useMemo, useRef, useState, useSyncExternalStore, type DragEvent, type KeyboardEvent, type MouseEvent, type ReactNode } from 'react';
import { ActionMenu, type MenuEntry } from '../components/Menu';
import { Check } from '../components/Check';
import { Dropdown } from '../components/Dropdown';
import { confirmDialog } from '../components/Ask';
import { toast } from '../state/store';
import { projectListSnapshot, refreshProjects, subscribeProjectList, useProject } from '../flow/project';
import { createSelection, reduceSelection, selectedVisible, type SelectionEvent } from '../profiles/selection';
import { deleteProject, duplicateProject, listTrash, openProject, useProjects, type ProjectMeta } from './api';
import { fileVerbs, newProjectFrom } from './verbs';
import { setProjectsQuery, useProjectsQuery } from './query';
import { projectStore, type ProjectCommandStatus, type ProjectStore } from '../persist/backend';
import { preferenceJson, preferenceText, savedPreferencesSnapshot, savePreferences, subscribeSavedPreferences, usePreference } from '../saved-preferences';
import './projects.css';

const noCommands: readonly Readonly<ProjectCommandStatus>[] = Object.freeze([]);
const noSubscribe = () => () => {};
const emptyCommands = () => noCommands;
type ProjectUndo = {id:string;name:string;run:()=>Promise<void>};
const projectUndos = new Map<string,ProjectUndo>();
const trashRestores = new Map<string,number|undefined>();
const commandVerb = {rename:'Rename',duplicate:'Duplicate',archive:'Delete',restore:'Restore'};

type Filter = 'all'|'recent'|'favorites'|'trash';
type Sort = 'edited'|'name'|'objects';
type View = 'grid'|'list';
const FILTERS: {id:Filter;label:string}[] = [{id:'all',label:'All projects'},{id:'recent',label:'Recent'},{id:'favorites',label:'Favorites'},{id:'trash',label:'Trash'}];
const SORTS = [{value:'edited',label:'Last edited'},{value:'name',label:'Name'},{value:'objects',label:'Objects'}] as const;
const RECENT_MS = 7 * 86400_000;
let lastFilter: Filter = 'all';

const ago = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto', style: 'short' });
function edited(t: number) {
  if(!t)return '—';
  const s = (t - Date.now()) / 1000;
  if (s > -60) return ago.format(0, 'minute');
  if (s > -3600) return ago.format(Math.round(s / 60), 'minute');
  if (s > -86400) return ago.format(Math.round(s / 3600), 'hour');
  if (s > -7 * 86400) return ago.format(Math.round(s / 86400), 'day');
  return new Date(t).toLocaleDateString();
}
const objects = (n: number) => `${n} ${n===1?'object':'objects'}`;
const message = (error: unknown) => error instanceof Error ? error.message : String(error);
const names = (list: readonly {name:string}[]) => list.length <= 5 ? list.map(p=>p.name).join('\n') : `${list.slice(0,5).map(p=>p.name).join('\n')}\n+${list.length-5} more`;
const sorters: Record<Sort,(a:ProjectMeta,b:ProjectMeta)=>number> = {
  edited: (a,b)=>b.savedAt-a.savedAt,
  name: (a,b)=>a.name.localeCompare(b.name,undefined,{numeric:true,sensitivity:'base'}) || b.savedAt-a.savedAt,
  objects: (a,b)=>b.objects-a.objects || b.savedAt-a.savedAt,
};

async function exportProject(p:ProjectMeta) {
  if(!await confirmDialog('Export 3MF',`Open “${p.name}” and export it?`,['Export','Cancel']))return;
  try {const flow=await import('../flow/project');await flow.openProject(p.id);if(flow.project.get().context.id!==p.id || ['missing','failed','opening'].includes(flow.project.get().value))throw new Error('The project could not be opened.');await (await import('../actions')).saveProject();}
  catch(error){toast(`Export failed: ${message(error)}`);}
}

function Thumb({p}:{p:ProjectMeta}) {
  return p.thumbnail ? <img src={p.thumbnail} alt="" draggable={false} loading="lazy" decoding="async"/> : <svg className="pj-placeholder" viewBox="0 0 24 24" aria-hidden><path d="M12 3 4 7.5v9L12 21l8-4.5v-9L12 3Zm0 2.3 5.6 3.2L12 11.7 6.4 8.5 12 5.3ZM6 10.2l5 2.9v5.6l-5-2.8v-5.7Zm7 8.5v-5.6l5-2.9v5.7l-5 2.8Z" fill="currentColor"/></svg>;
}
function Star({on}:{on:boolean}) {
  return <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden><path d="m12 3.5 2.6 5.3 5.9.9-4.3 4.1 1 5.8L12 16.9l-5.2 2.7 1-5.8-4.3-4.1 5.9-.9L12 3.5Z" fill={on?'currentColor':'none'} stroke="currentColor" strokeWidth="1.6" strokeLinejoin="round"/></svg>;
}

function Item({p,view,trash,current,selected,selecting,focused,favorite,canFavorite,renaming,blocked,setRenaming,select,onMenu,onFavorite,onRestore}: {
  p:ProjectMeta;view:View;trash:boolean;current:boolean;selected:boolean;selecting:boolean;focused:boolean;favorite:boolean;canFavorite:boolean;renaming:boolean;blocked:boolean;
  setRenaming(on:boolean):void;select(event:SelectionEvent):void;onMenu(at:{x:number;y:number}):void;onFavorite():void;onRestore():void;
}) {
  const committed=useRef(false),[renameError,setRenameError]=useState<string|null>(null),[renameSaving,setRenameSaving]=useState(false);
  const commit=async(name:string)=>{if(committed.current)return;if(!name.trim()){setRenameError('Enter a name.');return;}if(name.trim()===p.name){setRenaming(false);return;}committed.current=true;setRenameSaving(true);try{await (await import('../flow/project')).renameProject(p.id,name.trim());setRenameError(null);setRenaming(false);}catch(error){setRenameError(message(error));committed.current=false;}finally{setRenameSaving(false);}};
  useEffect(()=>{if(renaming)committed.current=false;},[renaming]);
  const click=(e:MouseEvent<HTMLElement>)=>{
    if(renaming || (e.target as Element).closest('button,input,.pj-check'))return;
    e.currentTarget.focus();
    if(trash)return;
    if(e.metaKey||e.ctrlKey||e.shiftKey||selecting)select({type:'CLICK',key:p.id,toggle:!e.shiftKey,shift:e.shiftKey});
    else void openProject(p.id);
  };
  const more=(e:MouseEvent<HTMLButtonElement>)=>{const r=e.currentTarget.getBoundingClientRect();onMenu({x:r.left,y:r.bottom+2});};
  const name=renaming
    ? <input className="text pj-rename" disabled={renameSaving || blocked} aria-busy={renameSaving} autoFocus defaultValue={p.name} aria-label="Project name" onClick={e=>e.stopPropagation()} onFocus={e=>e.currentTarget.select()} onBlur={e=>commit(e.currentTarget.value)} onKeyDown={e=>{e.stopPropagation();if(e.key==='Enter')commit(e.currentTarget.value);else if(e.key==='Escape'){committed.current=true;setRenaming(false);}}}/>
    : <span className="pj-name">{p.name}</span>;
  const check=!trash && <span className="pj-check"><Check checked={selected} aria-label={`Select ${p.name}`} onCheckedChange={()=>select({type:'CLICK',key:p.id,toggle:true})}/></span>;
  const actions=trash
    ? <button type="button" className="btn pj-restore" disabled={blocked} onClick={onRestore}>Restore</button>
    : <>
      <button type="button" className={`icon-btn pj-icon pj-fav${favorite?' on':''}`} aria-label="Favorite" aria-pressed={favorite} disabled={!canFavorite} onClick={onFavorite}><Star on={favorite}/></button>
      <button type="button" className="icon-btn pj-icon" aria-label={`Actions for ${p.name}`} aria-haspopup="menu" onClick={more}><svg viewBox="0 0 24 24" width="16" height="16" aria-hidden><circle cx="5" cy="12" r="1.8" fill="currentColor"/><circle cx="12" cy="12" r="1.8" fill="currentColor"/><circle cx="19" cy="12" r="1.8" fill="currentColor"/></svg></button>
    </>;
  const info=<>{current && <span className="pj-badge">Open</span>}<span>{objects(p.objects)}</span></>;
  return <div className={`pj-item${trash?' trash':''}${current?' current':''}${selected?' selected':''}${selecting?' selecting':''}`} role="row" tabIndex={focused?0:-1} data-id={p.id} aria-label={p.name} aria-selected={trash?undefined:selected} aria-current={current || undefined}
    onClick={click} onContextMenu={e=>{if(trash || renaming)return;e.preventDefault();if(!selected)select({type:'CLICK',key:p.id});onMenu({x:e.clientX,y:e.clientY});}}>
    {view==='grid' ? <>
      <div className="pj-thumb" role="gridcell"><Thumb p={p}/>{check}<span className="pj-overlay" onClick={e=>e.stopPropagation()}>{actions}</span></div>
      <div className="pj-meta" role="gridcell">{name}{renameError && <span className="pj-rename-error" role="alert">{renameError}</span>}
        <span className="pj-sub">{info}<span aria-hidden>·</span><time dateTime={p.savedAt?new Date(p.savedAt).toISOString():undefined}>{edited(p.savedAt)}</time></span></div>
    </> : <>
      <div className="pj-cell pj-lead" role="gridcell">{check}<span className="pj-mini"><Thumb p={p}/></span></div>
      <div className="pj-cell pj-title" role="gridcell">{name}{renameError && <span className="pj-rename-error" role="alert">{renameError}</span>}</div>
      <div className="pj-cell pj-col-objects" role="gridcell">{info}</div>
      <div className="pj-cell pj-col-edited" role="gridcell"><time dateTime={p.savedAt?new Date(p.savedAt).toISOString():undefined}>{edited(p.savedAt)}</time></div>
      <div className="pj-cell pj-row-actions" role="gridcell" onClick={e=>e.stopPropagation()}>{actions}</div>
    </>}
  </div>;
}

class Boundary extends Component<{children:ReactNode},{failed:boolean}> {
  state={failed:false};static getDerivedStateFromError(){return {failed:true};}
  componentDidCatch(error:unknown){console.warn('Projects',error);}
  render(){return this.state.failed ? <div className="projects"><div className="pj-empty" role="alert"><strong>Projects unavailable</strong><button type="button" className="btn" onClick={()=>{refreshProjects();this.setState({failed:false});}}>Retry</button></div></div> : this.props.children;}
}
export default function ProjectsView(){return <Boundary><Projects/></Boundary>;}

function Projects() {
  const list=useProjects(),current=useProject(s=>s.context.id),status=useSyncExternalStore(subscribeProjectList,projectListSnapshot);
  const [storage,setStorage]=useState<ProjectStore|null>(null);
  useEffect(()=>{let active=true;void projectStore().then(store=>{if(active)setStorage(store);});return()=>{active=false;};},[status.state==='ready']);
  const commands=useSyncExternalStore(storage ? storage.subscribeCommands : noSubscribe,storage ? storage.commands : emptyCommands);
  const blocked=(id:string)=>commands.some(c=>c.id===id);
  usePreference('projects.favorites');usePreference('projects.view');usePreference('projects.sort');
  const favoriteIds=preferenceJson<unknown>('projects.favorites',[]),favorites=new Set(Array.isArray(favoriteIds)?favoriteIds.filter((id):id is string=>typeof id==='string'):[]),favoriteKey=[...favorites].join('\n');
  const view:View=preferenceText('projects.view')==='list'?'list':'grid';
  const sortPref=preferenceText('projects.sort') ?? 'edited',sort:Sort=Object.hasOwn(sorters,sortPref)?sortPref as Sort:'edited';
  const prefsLoaded=useSyncExternalStore(subscribeSavedPreferences,savedPreferencesSnapshot).hydrated;
  const toggleFavorite=(id:string)=>{if(!prefsLoaded)return;const next=new Set(favorites);if(!next.delete(id))next.add(id);savePreferences({'projects.favorites':[...next]});};
  const [filter,setFilterState]=useState<Filter>(lastFilter);
  const setFilter=(next:Filter)=>{lastFilter=next;setFilterState(next);setRenaming(null);setMenu(null);};
  const [trash,setTrash]=useState<ProjectMeta[]|null>(null),[trashError,setTrashError]=useState<string|null>(null);
  const loadTrash=()=>listTrash().then(rows=>{setTrash(rows);setTrashError(null);},error=>setTrashError(message(error)));
  useEffect(()=>{void loadTrash();},[list]);
  const query=useProjectsQuery(),q=query.trim().toLocaleLowerCase();
  const counts=useMemo(()=>{const now=Date.now(),all=list??[];return {all:all.length,recent:all.filter(p=>now-p.savedAt<RECENT_MS).length,favorites:all.filter(p=>favorites.has(p.id)).length,trash:trash?.length};},[list,trash,favoriteKey]);
  const source=filter==='trash'?trash:list;
  const shown=useMemo(()=>{
    const now=Date.now();
    return (source??[]).filter(p=>(filter!=='recent' || now-p.savedAt<RECENT_MS) && (filter!=='favorites' || favorites.has(p.id)) && (!q || p.name.toLocaleLowerCase().includes(q))).sort(sorters[sort]);
  },[source,filter,q,sort,favoriteKey]);
  const visible=useMemo(()=>filter==='trash'?[]:shown.map(p=>p.id),[shown,filter]);
  const [raw,setSelection]=useState(()=>createSelection('projects',visible)),selection=useMemo(()=>reduceSelection(raw,{type:'SYNC',scope:'projects',visible}),[raw,visible]);
  const latest=useRef(selection);latest.current=selection;
  const selected=selectedVisible(selection),selectedSet=useMemo(()=>new Set(selected),[selection]);
  const [renaming,setRenaming]=useState<string|null>(null),[busy,setBusy]=useState(false),[error,setError]=useState<string|null>(null);
  const [undoBusy,setUndoBusy]=useState(false),[retrying,setRetrying]=useState<string|null>(null);
  const [menu,setMenu]=useState<{id:string;x:number;y:number}|null>(null),grid=useRef<HTMLDivElement>(null);
  const [trashFocus,setTrashFocus]=useState<string|null>(null);
  const focusKey=filter==='trash'?(shown.some(p=>p.id===trashFocus)?trashFocus:shown[0]?.id):selection.focus??visible[0];
  const dispatch=(event:SelectionEvent,focus=false)=>{const next=reduceSelection(latest.current,event);latest.current=next;setSelection(next);if(focus && next.focus)requestAnimationFrame(()=>{const row=[...(grid.current?.querySelectorAll<HTMLElement>('[data-id]')??[])].find(el=>el.dataset.id===next.focus);row?.focus();row?.scrollIntoView({block:'nearest'});});};
  const restore=async(entries:readonly ProjectUndo[])=>{
    if(undoBusy)return;
    setUndoBusy(true);let restored=0;const failed:string[]=[];
    try{
      for(const entry of entries)try{await entry.run();if(projectUndos.get(entry.id)===entry)projectUndos.delete(entry.id);restored++;}
      catch(error){failed.push(`${entry.name}: ${message(error)}`);}
      setError(failed.length?`${restored} restored, ${failed.length} not confirmed (${failed.join('; ')}). Undo stays available.`:null);
      if(restored)toast(`${restored} ${restored===1?'project':'projects'} restored`);
    }finally{setUndoBusy(false);void loadTrash();}
  };
  const restoreFromTrash=async(p:ProjectMeta)=>{
    const undo=projectUndos.get(p.id);
    if(undo)return restore([undo]);
    if(retrying)return;
    if(!trashRestores.has(p.id))trashRestores.set(p.id,p.revision);
    setRetrying(p.id);setError(null);
    try{await (await import('../flow/project')).restoreArchivedProject(p.id,trashRestores.get(p.id));trashRestores.delete(p.id);toast(`${p.name} restored`);}
    catch(error){setError(`Restore not confirmed: ${message(error)}`);}
    finally{setRetrying(null);void loadTrash();}
  };
  const retry=async(command:Readonly<ProjectCommandStatus>)=>{
    if(retrying)return;setRetrying(command.operationId);setError(null);
    try{
      const flow=await import('../flow/project');
      if(command.kind==='rename'){await flow.renameProject(command.id,command.name!);setRenaming(id=>id===command.id?null:id);}
      else if(command.kind==='duplicate')await flow.duplicateProject(command.id);
      else if(command.kind==='archive'){
        const run=await flow.deleteProject(command.id);projectUndos.set(command.id,{id:command.id,name:list?.find(p=>p.id===command.id)?.name ?? command.id,run});
      }else if(projectUndos.has(command.id))await restore([projectUndos.get(command.id)!]);
      else if(trashRestores.has(command.id)){await flow.restoreArchivedProject(command.id,trashRestores.get(command.id));trashRestores.delete(command.id);}
      else throw new Error('Retry from the original Undo.');
    }catch(error){setError(message(error));}
    finally{setRetrying(null);void loadTrash();}
  };
  const dismiss=async(command:Readonly<ProjectCommandStatus>)=>{
    if(retrying)return;
    setRetrying(command.operationId);
    const undo=projectUndos.get(command.id);
    try{
      await (await import('../flow/project')).discardRejectedProjectCommand(command.operationId);
      if(command.kind==='restore' && projectUndos.get(command.id)===undo)projectUndos.delete(command.id);
      if(command.kind==='restore')trashRestores.delete(command.id);
      setError(null);
      await refreshProjects();
    }catch(error){setError(message(error));}
    finally{setRetrying(null);}
  };
  const remove=async(ids:readonly string[])=>{
    const targets=(list??[]).filter(p=>ids.includes(p.id));if(busy || !targets.length || targets.some(p=>blocked(p.id)))return;
    if(!await confirmDialog(targets.length===1?'Move to Trash?':`Move ${targets.length} projects to Trash?`,names(targets),['Move to Trash','Cancel'],true))return;
    setBusy(true);setError(null);const undo:ProjectUndo[]=[],failed:string[]=[];
    try{for(const p of targets){const run=await deleteProject(p.id);if(run){const entry={id:p.id,name:p.name,run};undo.push(entry);projectUndos.set(p.id,entry);}else failed.push(p.name);}
      setSelection(s=>reduceSelection(s,{type:'SYNC',scope:'projects',visible:visible.filter(id=>!undo.some(p=>p.id===id))}));
      if(failed.length)setError(`${undo.length} moved to Trash, ${failed.length} not confirmed: ${failed.join(', ')}.`);
      if(undo.length)toast(`${undo.length===1?undo[0].name:`${undo.length} projects`} moved to Trash`,{label:'Undo',run:()=>void restore(undo)},10000);
    }finally{setBusy(false);}
  };
  const onKey=(e:KeyboardEvent)=>{
    const command=e.ctrlKey||e.metaKey,focus=(e.target as Element).closest<HTMLElement>('.pj-item[data-id]')?.dataset.id ?? focusKey;
    if((e.target as Element).closest('input,textarea,[contenteditable=true]') || busy)return;
    if((e.target as Element).closest('button') && !(command&&e.key.toLowerCase()==='a') && e.key!=='Escape')return;
    if(filter==='trash'){
      if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(e.key)){
        const rows=[...(grid.current?.querySelectorAll<HTMLElement>('[data-id]')??[])],at=rows.findIndex(r=>r.dataset.id===focus);
        const next=e.key==='Home'?0:e.key==='End'?rows.length-1:Math.max(0,Math.min(rows.length-1,at+(['ArrowLeft','ArrowUp'].includes(e.key)?-1:1)));
        const row=rows[next];if(row?.dataset.id){setTrashFocus(row.dataset.id);row.focus();row.scrollIntoView({block:'nearest'});}
      }else return;
      e.preventDefault();e.stopPropagation();return;
    }
    if(command && e.key.toLowerCase()==='a')dispatch({type:'SELECT_ALL'});
    else if(e.key==='Escape')dispatch({type:'ESCAPE'});
    else if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(e.key)){
      const items=[...(grid.current?.querySelectorAll<HTMLElement>('.pj-item')??[])],cols=Math.max(1,items.filter(c=>c.offsetTop===items[0]?.offsetTop).length);
      const direction=e.key==='Home'?'first':e.key==='End'?'last':['ArrowLeft','ArrowUp'].includes(e.key)?'previous':'next',steps=e.key==='ArrowUp'||e.key==='ArrowDown'?cols:1;
      for(let i=0;i<steps;i++)dispatch({type:'ARROW',direction,shift:e.shiftKey,focusOnly:command},i===steps-1);
    }else if(e.key==='Enter' && focus)void openProject(focus);
    else if(e.key===' ' && focus)dispatch({type:'CLICK',key:focus,toggle:true,shift:e.shiftKey});
    else if(e.key==='F2' && selected.length===1 && !blocked(selected[0]))setRenaming(selected[0]);
    else if(e.key==='Delete' || e.key==='Backspace')void remove(selected.length?selected:focus?[focus]:[]);
    else if((e.key==='ContextMenu'||e.shiftKey&&e.key==='F10') && focus){const row=[...(grid.current?.querySelectorAll<HTMLElement>('[data-id]')??[])].find(el=>el.dataset.id===focus);if(row){const r=row.getBoundingClientRect();setMenu({id:focus,x:r.left+8,y:r.bottom-8});}}
    else return;e.preventDefault();e.stopPropagation();
  };
  const onDrop=(e:DragEvent)=>{if(!e.dataTransfer.files.length)return;e.preventDefault();void newProjectFrom(e.dataTransfer.files);};
  const menuProject=menu && list?.find(p=>p.id===menu.id);
  const menuItems=(p:ProjectMeta):MenuEntry[]=>[
    {label:'Open',action:()=>void openProject(p.id)},
    {label:'Rename',shortcut:'F2',disabled:blocked(p.id),action:()=>setRenaming(p.id)},
    {label:'Duplicate',disabled:blocked(p.id),action:()=>void duplicateProject(p.id)},
    {label:favorites.has(p.id)?'Remove from favorites':'Add to favorites',disabled:!prefsLoaded,action:()=>toggleFavorite(p.id)},
    {label:'Export 3MF…',action:()=>void exportProject(p)},
    'sep',
    {label:'Move to Trash',shortcut:'Del',disabled:busy || blocked(p.id),action:()=>void remove(selected.length>1 && selected.includes(p.id)?selected:[p.id])},
  ];
  const title=FILTERS.find(f=>f.id===filter)!.label,loading=filter==='trash'?!trash && !trashError:!list;
  const failed=filter==='trash'?trashError:status.state==='failed'?status.error:null;
  const empty=filter==='trash'?'Trash is empty':filter==='favorites'?'No favorites':filter==='recent'?'Nothing edited this week':'No projects yet';
  return <div className="projects" onDragOver={e=>e.preventDefault()} onDrop={onDrop}><div className="pj-layout">
    <nav className="pj-side" aria-label="Project filters">
      {FILTERS.map(f=><button key={f.id} type="button" className={`pj-filter${filter===f.id?' on':''}`} aria-current={filter===f.id?'page':undefined} onClick={()=>setFilter(f.id)}>
        <span>{f.label}</span>{counts[f.id]!==undefined && <span className="pj-n">{counts[f.id]}</span>}
      </button>)}
    </nav>
    <section className="pj-main" aria-label={title}>
      <header className="pj-bar">
        <h1>{title}</h1>
        <input className="text pj-search" type="search" placeholder="Search" aria-label="Search projects" value={query} onChange={e=>setProjectsQuery(e.target.value)} onKeyDown={e=>{if(e.key==='Escape' && query){e.stopPropagation();setProjectsQuery('');}}}/>
        <div className="pj-tools">
          <Dropdown className="pj-sort" aria-label="Sort by" options={SORTS} value={sort} onValueChange={v=>savePreferences({'projects.sort':v})}/>
          <div className="seg pj-view" role="group" aria-label="View">
            <button type="button" aria-pressed={view==='grid'} aria-label="Grid" onClick={()=>savePreferences({'projects.view':'grid'})}><svg viewBox="0 0 16 16" width="14" height="14" aria-hidden><path d="M2 2h5v5H2zM9 2h5v5H9zM2 9h5v5H2zM9 9h5v5H9z" fill="currentColor"/></svg></button>
            <button type="button" aria-pressed={view==='list'} aria-label="List" onClick={()=>savePreferences({'projects.view':'list'})}><svg viewBox="0 0 16 16" width="14" height="14" aria-hidden><path d="M2 3h12v2H2zM2 7h12v2H2zM2 11h12v2H2z" fill="currentColor"/></svg></button>
          </div>
          <button type="button" className="btn" onClick={fileVerbs.import}>Import</button>
          <button type="button" className="btn-primary" onClick={fileVerbs.new}>New project</button>
        </div>
      </header>
      {failed && !loading && <div className="pj-line" role="alert"><span>{failed}</span><button type="button" className="btn" onClick={()=>{if(filter==='trash')void loadTrash();else void refreshProjects();}}>Retry</button></div>}
      {error && <div className="pj-line" role="alert"><span>{error}</span><button type="button" className="btn-ghost" onClick={()=>setError(null)}>Dismiss</button></div>}
      {commands.map(command=>{
        const pending=command.state==='preparing' || command.state==='sending',name=list?.find(p=>p.id===command.id)?.name ?? trash?.find(p=>p.id===command.id)?.name ?? command.name ?? 'Project';
        const state=pending?'Saving…':command.state==='acknowledged'?'Saved':command.state==='conflict'?`Changed elsewhere: ${command.error}`:`Not saved: ${command.error}`;
        return <div className={`pj-line${command.state==='failed'||command.state==='conflict'?' bad':''}`} key={command.operationId} aria-busy={pending} role="status">
          <span><strong>{commandVerb[command.kind]} {name}</strong> · {state}</span>
          {!pending && (command.kind!=='restore' || projectUndos.has(command.id) || trashRestores.has(command.id)) && <button type="button" className="btn" disabled={!!retrying || undoBusy} onClick={()=>void retry(command)}>{command.state==='acknowledged'?'Finish':'Retry'}</button>}
          {command.state==='conflict' && <button type="button" className="btn-ghost" disabled={!!retrying || undoBusy} onClick={()=>void dismiss(command)}>Dismiss</button>}
        </div>;
      })}
      {!!projectUndos.size && filter!=='trash' && <div className="pj-line"><span>{projectUndos.size} moved to Trash</span><button type="button" className="btn" disabled={undoBusy} onClick={()=>void restore([...projectUndos.values()])}>{undoBusy?'Restoring…':'Undo'}</button></div>}
      {selected.length>0 && filter!=='trash' && <div className="pj-line pj-selection">
        <strong>{selected.length} selected</strong>
        {selected.length===1 && <>
          <button type="button" className="btn-ghost" onClick={()=>void openProject(selected[0])}>Open</button>
          <button type="button" className="btn-ghost" disabled={blocked(selected[0])} onClick={()=>setRenaming(selected[0])}>Rename</button>
          <button type="button" className="btn-ghost" disabled={blocked(selected[0])} onClick={()=>void duplicateProject(selected[0])}>Duplicate</button>
        </>}
        <button type="button" className="btn-ghost" disabled={busy || selected.some(blocked)} onClick={()=>void remove(selected)}>Move to Trash</button>
        <button type="button" className="btn-ghost pj-clear" onClick={()=>dispatch({type:'CLEAR'})}>Clear</button>
      </div>}
      {loading ? <div className="pj-empty" role="status">Loading…</div>
        : !shown.length ? <div className="pj-empty">{failed ? null : q ? <><strong>No matches</strong><button type="button" className="btn" onClick={()=>setProjectsQuery('')}>Clear search</button></>
          : <><strong>{empty}</strong>{filter==='all' && <div className="pj-empty-actions"><button type="button" className="btn-primary" onClick={fileVerbs.new}>New project</button><button type="button" className="btn" onClick={fileVerbs.import}>Import model</button></div>}</>}</div>
        : <div ref={grid} className={`pj-${view}`} role="grid" aria-label={title} aria-multiselectable={filter==='trash'?undefined:true} aria-busy={busy || status.state==='loading'} onKeyDown={onKey}>
          {view==='list' && <div className="pj-head" role="row"><span role="columnheader" className="pj-lead"/><span role="columnheader">Name</span><span role="columnheader" className="pj-col-objects">Objects</span><span role="columnheader" className="pj-col-edited">Edited</span><span role="columnheader" className="pj-row-actions"/></div>}
          {shown.map(p=><Item key={p.id} p={p} view={view} trash={filter==='trash'} current={filter!=='trash' && p.id===current} selected={selectedSet.has(p.id)} selecting={selected.length>0} focused={p.id===focusKey} favorite={favorites.has(p.id)} canFavorite={prefsLoaded} renaming={renaming===p.id} blocked={blocked(p.id) || retrying===p.id}
            setRenaming={on=>{if(!on || !blocked(p.id))setRenaming(on?p.id:null);}} select={event=>dispatch(event)} onMenu={at=>setMenu({id:p.id,...at})} onFavorite={()=>toggleFavorite(p.id)} onRestore={()=>void restoreFromTrash(p)}/>)}
        </div>}
    </section></div>
    {menu && menuProject && <ActionMenu label="Project actions" at={menu} onClose={()=>setMenu(null)} items={menuItems(menuProject)}/>}
  </div>;
}
