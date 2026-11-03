import { useState } from 'react';
import { useProject } from '../flow/project';
import { renameProject, useProjects } from './api';
import { useFolders } from './folders';
import { setState, useStore } from '../state/store';

const SAVED = { editing: 'Unsaved', saving: 'Saving…', saved: 'Saved', opening: 'Opening…', failed: 'Not saved', missing: 'Not saved' } as const;

export function ProjectTitle() {
  const name = useStore((s) => (s.projectName ?? s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf|drc)$/i, '')) ?? 'Untitled';
  const state = useProject((p) => p.value);
  const id = useProject((p) => p.context.id);
  const conflict = useProject((p) => p.context.conflict);
  const folderId = useProjects()?.find((p) => p.id === id)?.folderId;
  const folders = useFolders().folders;
  const folder = folderId ? folders.find((f) => f.id === folderId)?.name : undefined;
  const [editing, setEditing] = useState(false);
  const commit = (v: string) => {
    setEditing(false);
    const next = v.trim();
    if (!next || next === name) return;
    if (id && state !== 'blank') void renameProject(id, next);
    else setState({ projectName: next });
  };
  const saved = conflict ? 'Conflict' : SAVED[state as keyof typeof SAVED];
  return (
    <div className="topbar-title">
      {folder && !editing && <span className="title-folder">{folder} /</span>}
      {editing ? (
        <input className="title-edit" autoFocus defaultValue={name} aria-label="Name" size={Math.max(8, name.length + 2)}
          onFocus={(e) => e.currentTarget.select()} onBlur={(e) => commit(e.currentTarget.value)}
          onKeyDown={(e) => { e.stopPropagation(); if (e.key === 'Enter') commit(e.currentTarget.value); else if (e.key === 'Escape') setEditing(false); }} />
      ) : (
        <button type="button" className="title-name" aria-description="Rename" onClick={() => setEditing(true)}
          onKeyDown={(e) => { if (e.key === 'F2') { e.preventDefault(); e.stopPropagation(); setEditing(true); } }}>
          <span className="title-text">{name}</span>
          <svg className="title-pencil" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M3.5 12.5l.8-3 6.9-6.9 2.2 2.2-6.9 6.9-3 .8zM9.9 3.9l2.2 2.2" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round"/></svg>
        </button>
      )}
      {saved && !editing && <span className="title-state" role="status">{saved}</span>}
    </div>
  );
}
