import { useEffect, useRef, type ReactNode } from 'react';
import { setSettingsSheet, useNarrow, useSettingsSheet } from '../workspace-sheet';
import { Glyph } from './Glyph';
export default function WorkspaceRail({children}:{children:ReactNode}) {
  const narrow=useNarrow(),sheet=useSettingsSheet(),open=narrow && sheet;
  const panel=useRef<HTMLDivElement>(null),launch=useRef<HTMLButtonElement>(null),done=useRef<HTMLButtonElement>(null);
  useEffect(()=>{if(!narrow)setSettingsSheet(false);},[narrow]);
  useEffect(()=>{
    const canvas=panel.current?.parentElement?.querySelector<HTMLElement>('.canvas-area');
    if(canvas)canvas.inert=open;
    if(open)done.current?.focus();
    return()=>{if(canvas)canvas.inert=false;};
  },[open]);
  const close=()=>{setSettingsSheet(false);requestAnimationFrame(()=>launch.current?.focus());};
  return <>{narrow && !open && <button ref={launch} type="button" className="icon-btn canvas-chip workspace-settings-launch" aria-label="Settings" aria-controls="workspace-settings" aria-expanded={false} onClick={()=>setSettingsSheet(true)}><Glyph name="rail"/></button>}
    {open && <div className="workspace-scrim" aria-hidden onClick={close}/>}
    <div ref={panel} id="workspace-settings" className="workspace-rail" data-open={open} role={open?'dialog':undefined} aria-label={open?'Settings':undefined} onKeyDown={event=>{
      if(open && event.key==='Escape' && !event.defaultPrevented && !(event.target as Element).closest('[role=menu],[role=listbox]') && (event.target as Element).closest('[role=dialog]')===panel.current){event.preventDefault();event.stopPropagation();close();}
    }}>{open && <header className="workspace-sheet-heading"><strong>Settings</strong><button ref={done} type="button" className="btn-primary" onClick={close}>Done</button></header>}{children}</div></>;
}
