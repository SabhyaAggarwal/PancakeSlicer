import { saveProject } from '../actions';
import { NoticeLog } from '../components/NoticeLog';
import { keepProjectVersion, reloadProject, retryProject, showProjects, useProject } from '../flow/project';
import { useProjectLease } from '../flow/project-lease';
import { projectsHref } from '../flow/routes';
import './projects.css';

export function ProjectStatus() {
  return <><LifecycleStatus /><NoticeLog /></>;
}

function LifecycleStatus() {
  const value = useProject((s) => s.value);
  if (value === 'missing') return (
    <div className="pj-status" role="status"><span>Project not found</span>
      <a className="btn" href={projectsHref()} onClick={(e) => { e.preventDefault(); showProjects(); }}>Projects</a>
    </div>
  );
  if (value === 'opening') return <div className="pj-status" role="status"><span>Opening…</span></div>;
  return null;
}

export function ProjectBanner() {
  const { value, context } = useProject();
  const lease = useProjectLease().value;
  if (lease === 'viewing' || lease === 'lost') return null;
  if (value !== 'failed' && value !== 'conflict') return null;
  const conflict = value === 'conflict' || /another tab/.test(context.error ?? '');
  return (
    <div role="alert" className="lease-bar">
      <span>{conflict && context.failure?.name !== 'ProjectEmptyError' ? 'Changed in another window' : context.error ?? 'Not saved'}</span>
      <span className="lease-bar-actions">{context.loadError ? <>
        <button type="button" className="btn" onClick={() => void retryProject()}>Retry</button>
        <button type="button" className="btn" onClick={showProjects}>Projects</button>
      </> : conflict ? <>
        <button type="button" className="btn" onClick={() => void reloadProject()}>Reload latest</button>
        <button type="button" className="btn" onClick={() => void keepProjectVersion()}>Keep mine</button>
      </> : <>
        <button type="button" className="btn" onClick={() => void retryProject()}>Retry</button>
        {context.failure?.kind === 'interrupted'
          ? <button type="button" className="btn" onClick={() => void reloadProject()}>Reload saved</button>
          : <button type="button" className="btn" onClick={() => void saveProject()}>Export</button>}
      </>}</span>
    </div>
  );
}
