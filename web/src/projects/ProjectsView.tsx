import { Component, useEffect, useMemo, useRef, useState, useSyncExternalStore, type DragEvent, type KeyboardEvent, type ReactNode } from 'react';
import { ActionMenu } from '../components/Menu';
import { Check } from '../components/Check';
import { confirmDialog } from '../components/Ask';
import { toast } from '../state/store';
import { projectListSnapshot, refreshProjects, subscribeProjectList, useProject } from '../flow/project';
import { createSelection, reduceSelection, selectedVisible, type SelectionEvent } from '../profiles/selection';
import { deleteProject, duplicateProject, openProject, useProjects, type ProjectMeta } from './api';
import { fileVerbs, newProjectFrom } from './verbs';
import { setProjectsQuery, useProjectsQuery } from './query';
import { projectStore, type ProjectCommandStatus, type ProjectStore } from '../persist/backend';
import './projects.css';

const noCommands: readonly Readonly<ProjectCommandStatus>[] = Object.freeze([]);
const noSubscribe = () => () => {};
const emptyCommands = () => noCommands;
type ProjectUndo = {id:string;name:string;run:()=>Promise<void>};
const projectUndos = new Map<string,ProjectUndo>();
const commandVerb = {rename:'Rename',duplicate:'Duplicate',archive:'Delete',restore:'Undo delete'};

const ago = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto', style: 'short' });
function edited(t: number) {
  if(!t)return 'Update time unavailable';
  const s = (t - Date.now()) / 1000;
  if (s > -60) return ago.format(0, 'minute');
  if (s > -3600) return ago.format(Math.round(s / 60), 'minute');
  if (s > -86400) return ago.format(Math.round(s / 3600), 'hour');
  if (s > -7 * 86400) return ago.format(Math.round(s / 86400), 'day');
  return new Date(t).toLocaleDateString();
}

async function exportProject(p:ProjectMeta) {
  if(!await confirmDialog('Export project',`Open “${p.name}” and export its 3MF file?`,['Open and export','Cancel']))return;
  try {const flow=await import('../flow/project');await flow.openProject(p.id);if(flow.project.get().context.id!==p.id || ['missing','failed','opening'].includes(flow.project.get().value))throw new Error('This project could not be opened for export.');await (await import('../actions')).saveProject();}
  catch(error){toast(`Export failed: ${error instanceof Error?error.message:String(error)}`);}
}

function Card({p,current,selected,focused,renaming,blocked,setRenaming,select,onMenu}: {
  p:ProjectMeta;current:boolean;selected:boolean;focused:boolean;renaming:boolean;blocked:boolean;setRenaming(on:boolean):void;
  select(event:SelectionEvent):void;onMenu(at:{x:number;y:number}):void;
}) {
  const committed=useRef(false),[renameError,setRenameError]=useState<string|null>(null),[renameSaving,setRenameSaving]=useState(false);
  const commit=async(name:string)=>{if(committed.current)return;if(!name.trim()){setRenameError('Enter a project name.');return;}if(name.trim()===p.name){setRenaming(false);return;}committed.current=true;setRenameSaving(true);try{await (await import('../flow/project')).renameProject(p.id,name.trim());setRenameError(null);setRenaming(false);}catch(error){setRenameError(error instanceof Error?error.message:String(error));committed.current=false;}finally{setRenameSaving(false);}};
  useEffect(()=>{if(renaming)committed.current=false;},[renaming]);
  return <article className={`pj-card${current?' current':''}${selected?' selected':''}`} role="row" tabIndex={focused?0:-1} data-id={p.id} aria-label={p.name} aria-selected={selected} aria-current={current || undefined}
    onClick={e=>{if(!renaming){e.currentTarget.focus();select({type:'CLICK',key:p.id,toggle:e.metaKey||e.ctrlKey,shift:e.shiftKey});}}}
    onDoubleClick={e=>{if(!renaming && !(e.target as Element).closest('button,input'))void openProject(p.id);}}
    onContextMenu={e=>{e.preventDefault();if(!selected)select({type:'CLICK',key:p.id});onMenu({x:e.clientX,y:e.clientY});}}>
    <div className="pj-thumb" role="gridcell">{p.thumbnail ? <img src={p.thumbnail} alt="" draggable={false} loading="lazy"/> : <span className="pj-no-preview">No preview saved</span>}
      <span className="pj-check" onClick={e=>e.stopPropagation()}><Check checked={selected} aria-label={`Select ${p.name}`} onCheckedChange={()=>select({type:'CLICK',key:p.id,toggle:true})}/></span>
      {current && <span className="pj-current">Current project</span>}
    </div>
    <div className="pj-meta" role="gridcell">{renaming ? <input className="text pj-rename" disabled={renameSaving || blocked} aria-busy={renameSaving} autoFocus defaultValue={p.name} aria-label="Project name" onClick={e=>e.stopPropagation()} onFocus={e=>e.currentTarget.select()} onBlur={e=>commit(e.currentTarget.value)} onKeyDown={e=>{e.stopPropagation();if(e.key==='Enter')commit(e.currentTarget.value);else if(e.key==='Escape'){committed.current=true;setRenaming(false);}}}/> : <strong className="pj-name">{p.name}</strong>}
      {renameError && <span className="pj-rename-error" role="alert">{renameError}</span>}<span className="pj-time" title={p.savedAt ? new Date(p.savedAt).toLocaleString() : undefined}>{p.objects} {p.objects===1?'object':'objects'} · {edited(p.savedAt)}</span>
    </div>
    <div className="pj-card-actions" role="gridcell" onClick={e=>e.stopPropagation()}><button type="button" className="btn" onClick={()=>void openProject(p.id)}>Open</button><button type="button" className="btn-ghost pj-more" aria-label={`Actions for ${p.name}`} aria-haspopup="menu" onClick={e=>{const r=e.currentTarget.getBoundingClientRect();onMenu({x:r.left,y:r.bottom+2});}}>More</button></div>
  </article>;
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
  const query=useProjectsQuery(),q=query.trim().toLocaleLowerCase();
  const shown=useMemo(()=>(list??[]).filter(p=>!q || p.name.toLocaleLowerCase().includes(q)),[list,q]);
  const visible=useMemo(()=>shown.map(p=>p.id),[shown]);
  const [raw,setSelection]=useState(()=>createSelection('projects',visible)),selection=useMemo(()=>reduceSelection(raw,{type:'SYNC',scope:'projects',visible}),[raw,visible]);
  const latest=useRef(selection);latest.current=selection;
  const selected=selectedVisible(selection),selectedSet=useMemo(()=>new Set(selected),[selection]);
  const [renaming,setRenaming]=useState<string|null>(null),[busy,setBusy]=useState(false),[error,setError]=useState<string|null>(null);
  const [undoBusy,setUndoBusy]=useState(false),[retrying,setRetrying]=useState<string|null>(null);
  const [menu,setMenu]=useState<{id:string;x:number;y:number}|null>(null),grid=useRef<HTMLDivElement>(null);
  const dispatch=(event:SelectionEvent,focus=false)=>{const next=reduceSelection(latest.current,event);latest.current=next;setSelection(next);if(focus && next.focus)requestAnimationFrame(()=>{const row=[...(grid.current?.querySelectorAll<HTMLElement>('[data-id]')??[])].find(el=>el.dataset.id===next.focus);row?.focus();row?.scrollIntoView({block:'nearest'});});};
  const restore=async(entries:readonly ProjectUndo[])=>{
    if(undoBusy)return;
    setUndoBusy(true);let restored=0;const failed:string[]=[];
    try{
      for(const entry of entries)try{await entry.run();if(projectUndos.get(entry.id)===entry)projectUndos.delete(entry.id);restored++;}
      catch(error){failed.push(`${entry.name}: ${error instanceof Error?error.message:String(error)}`);}
      setError(failed.length?`${restored} restored; ${failed.length} Undo not confirmed. ${failed.join('; ')}. The original Undo requests are kept for Retry.`:null);
      if(restored)toast(`${restored} ${restored===1?'project':'projects'} restored`);
    }finally{setUndoBusy(false);}
  };
  const retry=async(command:Readonly<ProjectCommandStatus>)=>{
    if(retrying)return;setRetrying(command.operationId);setError(null);
    try{
      const flow=await import('../flow/project');
      if(command.kind==='rename'){await flow.renameProject(command.id,command.name!);setRenaming(id=>id===command.id?null:id);}
      else if(command.kind==='duplicate')await flow.duplicateProject(command.id);
      else if(command.kind==='archive'){
        const run=await flow.deleteProject(command.id);projectUndos.set(command.id,{id:command.id,name:list?.find(p=>p.id===command.id)?.name ?? command.id,run});
      }else {const entry=projectUndos.get(command.id);if(!entry)throw new Error('Retry from the original Undo action; its captured deletion owns this request.');await restore([entry]);}
    }catch(error){setError(error instanceof Error?error.message:String(error));}
    finally{setRetrying(null);}
  };
  const dismiss=async(command:Readonly<ProjectCommandStatus>)=>{
    if(retrying)return;
    setRetrying(command.operationId);
    const undo=projectUndos.get(command.id);
    try{
      await (await import('../flow/project')).discardRejectedProjectCommand(command.operationId);
      if(command.kind==='restore' && projectUndos.get(command.id)===undo)projectUndos.delete(command.id);
      setError('The rejected request was dismissed. Review the latest saved project before starting a new action.');
      await refreshProjects();
    }catch(error){setError(error instanceof Error?error.message:String(error));}
    finally{setRetrying(null);}
  };
  const remove=async(ids:readonly string[])=>{
    const targets=shown.filter(p=>ids.includes(p.id));if(busy || !targets.length || targets.some(p=>blocked(p.id)))return;
    if(!await confirmDialog(`Delete ${targets.length===1?'project':targets.length+' projects'}`,`Move to Trash?\n${targets.map(p=>p.name).join('\n')}`,['Delete','Cancel']))return;
    setBusy(true);setError(null);const undo:ProjectUndo[]=[],failed:string[]=[];
    try{for(const p of targets){const run=await deleteProject(p.id);if(run){const entry={id:p.id,name:p.name,run};undo.push(entry);projectUndos.set(p.id,entry);}else failed.push(p.name);}
      setSelection(s=>reduceSelection(s,{type:'SYNC',scope:'projects',visible:visible.filter(id=>!undo.some(p=>p.id===id))}));
      if(failed.length)setError(`${undo.length} deletes confirmed; ${failed.length} not confirmed: ${failed.join(', ')}. Retry the retained requests below.`);
      if(undo.length)toast(`${undo.length} ${undo.length===1?'project':'projects'} moved to Trash`,{label:'Undo confirmed deletes',run:()=>void restore(undo)},10000);
    }finally{setBusy(false);}
  };
  const onKey=(e:KeyboardEvent)=>{
    const command=e.ctrlKey||e.metaKey,focus=selection.focus??visible[0];
    if((e.target as Element).closest('input,textarea,[contenteditable=true]') || busy)return;
    if((e.target as Element).closest('button') && !(command&&e.key.toLowerCase()==='a') && e.key!=='Escape')return;
    if(command && e.key.toLowerCase()==='a')dispatch({type:'SELECT_ALL'});
    else if(e.key==='Escape')dispatch({type:'ESCAPE'});
    else if(['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(e.key)){
      const cards=[...(grid.current?.querySelectorAll<HTMLElement>('.pj-card')??[])],cols=Math.max(1,cards.filter(c=>c.offsetTop===cards[0]?.offsetTop).length);
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
  return <div className="projects" onDragOver={e=>e.preventDefault()} onDrop={onDrop}>
    <header className="pj-bar"><div className="pj-heading-context"><h1>Projects</h1><span className="pj-count">{list ? `${shown.length} of ${list.length} projects` : 'Loading library'}</span></div><input className="text pj-search" type="search" placeholder="Search projects" aria-label="Search projects" value={query} onChange={e=>setProjectsQuery(e.target.value)}/><button type="button" className="btn-primary" onClick={fileVerbs.new}>New project</button><button type="button" className="btn" onClick={fileVerbs.import}>Import</button></header>
    {status.state==='failed' && <div className="pj-feedback" role="alert"><span>{status.error}</span><button type="button" className="btn" onClick={refreshProjects}>Retry</button></div>}
    {error && <div className="pj-feedback" role="alert">{error}</div>}
    {!!commands.length && <section className="pj-commands" aria-label="Project requests" aria-live="polite">
      {commands.map(command=>{
        const pending=command.state==='preparing' || command.state==='sending',name=list?.find(p=>p.id===command.id)?.name ?? command.name ?? command.id;
        return <div className="pj-command" key={command.operationId} aria-busy={pending}>
          <div><strong>{commandVerb[command.kind]} · {name}</strong><span>{pending?'Waiting for service confirmation…':command.state==='acknowledged'?'Confirmed; finish the original action.':command.state==='conflict'?`Conflict: ${command.error}. The captured request has not been rebased.`:`Not confirmed: ${command.error}. Retry keeps the exact request.`}</span></div>
          {!pending && (command.kind!=='restore' || projectUndos.has(command.id)) && <button type="button" className="btn" disabled={!!retrying || undoBusy} onClick={()=>void retry(command)}>{command.state==='acknowledged'?'Finish action':'Retry exact request'}</button>}
          {command.state==='conflict' && <button type="button" className="btn" disabled={!!retrying || undoBusy} onClick={()=>void dismiss(command)}>Dismiss rejected request</button>}
        </div>;
      })}
    </section>}
    {!!projectUndos.size && <div className="pj-selection"><span>{projectUndos.size} confirmed {projectUndos.size===1?'delete':'deletes'} available to Undo</span><button type="button" className="btn" disabled={undoBusy} onClick={()=>void restore([...projectUndos.values()])}>{undoBusy?'Restoring…':'Undo confirmed deletes'}</button></div>}
    {selected.length>0 && <div className="pj-selection"><strong>{selected.length} selected</strong>{selected.length===1 && <><button type="button" className="btn" onClick={()=>void openProject(selected[0])}>Open</button><button type="button" className="btn-ghost" disabled={blocked(selected[0])} onClick={()=>setRenaming(selected[0])}>Rename</button><button type="button" className="btn-ghost" disabled={blocked(selected[0])} onClick={()=>void duplicateProject(selected[0])}>Duplicate</button></>}<button type="button" className="btn-ghost" disabled={busy || selected.some(blocked)} onClick={()=>void remove(selected)}>Delete{selected.length>1?' selected':''}</button><button type="button" className="btn-ghost" onClick={()=>dispatch({type:'CLEAR'})}>Clear selection</button></div>}
    {!list ? <div className="pj-empty" role="status">{status.state==='failed'?'Project library could not load.':'Loading projects…'}</div> : !shown.length ? <div className="pj-empty"><strong>{list.length?'No matching projects':'No projects yet'}</strong>{list.length ? <button type="button" className="btn" onClick={()=>setProjectsQuery('')}>Clear search</button> : <><span>Import a model or start a new project.</span><button type="button" className="btn" onClick={fileVerbs.import}>Import model</button></>}</div> : <div ref={grid} className="pj-grid" role="grid" aria-label="Projects" aria-multiselectable="true" aria-busy={busy || status.state==='loading'} onKeyDown={onKey}>{shown.map(p=><Card key={p.id} p={p} current={p.id===current} selected={selectedSet.has(p.id)} focused={p.id===(selection.focus??visible[0])} renaming={renaming===p.id} blocked={blocked(p.id)} setRenaming={on=>{if(!on || !blocked(p.id))setRenaming(on?p.id:null);}} select={event=>dispatch(event)} onMenu={at=>setMenu({id:p.id,...at})}/>)}</div>}
    {menu && menuProject && <ActionMenu label="Project actions" at={menu} onClose={()=>setMenu(null)} items={[{label:'Open',action:()=>void openProject(menuProject.id)},{label:'Rename',shortcut:'F2',disabled:blocked(menuProject.id),tip:'Resolve the pending project request first.',action:()=>setRenaming(menuProject.id)},{label:'Duplicate',disabled:blocked(menuProject.id),tip:'Resolve the pending project request first.',action:()=>void duplicateProject(menuProject.id)},{label:'Export 3MF…',action:()=>void exportProject(menuProject)},{label:'Delete',shortcut:'Del',disabled:busy || blocked(menuProject.id),tip:'Resolve the pending project request first.',action:()=>void remove([menuProject.id])}]}/>}
  </div>;
}
