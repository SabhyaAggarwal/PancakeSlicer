import { useState } from 'react';
import { useProject } from '../flow/project';
import { renameProject } from './api';
import { setState, useStore } from '../state/store';

const SAVED = { editing: 'Unsaved', saving: 'Saving…', opening: 'Opening…', failed: 'Not saved', missing: 'Not saved' } as const;

export function ProjectTitle() {
  const name = useStore((s) => (s.projectName ?? s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf|drc)$/i, '')) ?? 'Untitled';
  const state = useProject((p) => p.value);
  const id = useProject((p) => p.context.id);
  const [editing, setEditing] = useState(false);
  const commit = (v: string) => {
    setEditing(false);
    const next = v.trim();
    if (!next || next === name) return;
    if (id) void renameProject(id, next);
    else setState({ projectName: next });
  };
  const saved = SAVED[state as keyof typeof SAVED] ?? (state === 'blank' ? 'Rename' : 'Saved');
  return (
    <div className="topbar-title">
      {editing ? (
        <input className="title-edit" autoFocus defaultValue={name} aria-label="Name" size={Math.max(8, name.length + 2)}
          onFocus={(e) => e.currentTarget.select()} onBlur={(e) => commit(e.currentTarget.value)}
          onKeyDown={(e) => { e.stopPropagation(); if (e.key === 'Enter') commit(e.currentTarget.value); else if (e.key === 'Escape') setEditing(false); }} />
      ) : (
        <button type="button" className="title-name" aria-description={`${saved} · rename`} onClick={() => setEditing(true)}
          onKeyDown={(e) => { if (e.key === 'F2') { e.preventDefault(); e.stopPropagation(); setEditing(true); } }}>{name}</button>
      )}
    </div>
  );
}
