



import { lazy, Suspense, useEffect, useRef, useState, type ReactNode } from 'react';
import { retryProject, useProject } from '../flow/project';
import { renameProject, useProjects } from './api';
import { useFolders } from './folders';
import { setState, useStore } from '../state/store';
import { startVersionTracking, toggleHistory, useHistory, versionTime } from './versions';
import './title.css';
import './versions.css';

const VersionHistory = lazy(() => import('./VersionHistory'));

const clock = <svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true"><path d="M2.5 8a5.5 5.5 0 1 0 1.6-3.9M2.5 2.5v2.2h2.2M8 5v3.2l2.2 1.4" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round"/></svg>;

const spinner = <svg className="pt-spinner" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M8 2a6 6 0 0 1 6 6" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/></svg>;
const warning = <svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true"><path d="M8 2.2 14.3 13.5H1.7zM8 6.5v3.2M8 11.6v.1" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round"/></svg>;

export function ProjectTitle() {
  const name = useStore((s) => (s.projectName ?? s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf|drc)$/i, '')) ?? 'Untitled';
  const state = useProject((p) => p.value);
  const id = useProject((p) => p.context.id);

  const folderId = useProjects()?.find((p) => p.id === id)?.folderId;
  const folder = useFolders().folders.find((f) => f.id === folderId)?.name;
  const [draft, setDraft] = useState<string | null>(null);
  const cancelled = useRef(false);
  useEffect(startVersionTracking, []);
  const begin = () => { cancelled.current = false; setDraft(name); };
  const commit = () => {
    const next = draft?.trim();
    setDraft(null);
    if (cancelled.current || !next || next === name) return;

    if (id && state !== 'blank') void renameProject(id, next);
    else setState({ projectName: next });
  };
  return (
    <div className="project-title">
      <span className="pt-folder">{folder && draft === null ? `${folder} /` : ''}</span>
      {draft !== null ? (
        <span className="pt-box pt-editing" data-value={draft}>
          <input className="pt-input" autoFocus value={draft} aria-label="Project name" size={1} spellCheck={false}
            onChange={(e) => setDraft(e.currentTarget.value)} onFocus={(e) => e.currentTarget.select()} onBlur={commit}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === 'Enter') e.currentTarget.blur();
              else if (e.key === 'Escape') { cancelled.current = true; e.currentTarget.blur(); }
            }} />
        </span>
      ) : (
        <button type="button" className="pt-box pt-name" data-full-text={name} aria-description="Rename" onClick={begin}
          onKeyDown={(e) => { if (e.key === 'F2') { e.preventDefault(); e.stopPropagation(); begin(); } }}>
          <span className="pt-text">{name}</span>
          <svg className="pt-pencil" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true"><path d="M3.5 12.5l.8-3 6.9-6.9 2.2 2.2-6.9 6.9-3 .8zM9.9 3.9l2.2 2.2" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round"/></svg>
        </button>
      )}
      <span className="pt-end"><SaveState /></span>
    </div>
  );
}

const LABEL = { editing: 'Unsaved changes', saving: 'Saving…', saved: 'Saved' } as const;


function SaveState() {
  const { value, context } = useProject();
  const history = useHistory();
  const status = (tone: string, icon: ReactNode, label: string, props: { onClick(): void; hint?: string; expanded?: boolean }) => (
    <button type="button" className="pt-status" data-tone={tone} aria-expanded={props.expanded} aria-description={props.hint} onClick={props.onClick}>
      {icon}<span className="pt-label">{label}</span>
    </button>
  );
  const historyButton = (icon: ReactNode, label: string) => status('quiet', icon, label, { onClick: toggleHistory, expanded: history.open, hint: 'Version history' });
  let shown: ReactNode = null;
  if (value === 'conflict') shown = status('danger', warning, 'Conflict', { onClick: () => setState({ tab: 'prepare' }), hint: 'Choose Reload latest or Keep mine' });
  else if (history.viewing) shown = status('accent', clock, `Viewing ${versionTime(history.viewing.until)}`, { onClick: toggleHistory, expanded: history.open });
  else if (value === 'failed') shown = status('danger', warning, 'Retry', { onClick: () => void retryProject(), hint: context.error ?? 'The project was not saved' });
  else if (value === 'saving') shown = historyButton(<span className="pt-saving" aria-hidden="true">{spinner}{clock}</span>, LABEL.saving);
  else if (value === 'editing' || value === 'saved') shown = historyButton(clock, LABEL[value]);
  else if (value === 'opening') shown = <span className="pt-status" data-tone="quiet" role="status"><span className="pt-label">Opening…</span></span>;
  return <>{shown}{history.open && <Suspense fallback={null}><VersionHistory /></Suspense>}</>;
}
