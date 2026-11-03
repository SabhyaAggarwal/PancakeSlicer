import { saveProject } from '../actions';
import { NoticeLog } from '../components/NoticeLog';
import { keepProjectVersion, reloadProject, retryProject, showProjects, useProject } from '../flow/project';
import { projectsHref } from '../flow/routes';
import './projects.css';

export function ProjectStatus() {
  return <><LifecycleStatus /><NoticeLog /></>;
}

function LifecycleStatus() {
  const { value, context } = useProject();
  if (value === 'missing') return (
    <div className="pj-status" role="status"><span>Project not found</span>
      <a className="btn" href={projectsHref()} onClick={(e) => { e.preventDefault(); showProjects(); }}>Projects</a>
    </div>
  );
  if (value === 'opening') return <div className="pj-status" role="status"><span>Opening…</span></div>;
  if (value !== 'failed' && !context.conflict) return null;
  const conflict = context.conflict || /another tab/.test(context.error ?? '');
  return (
    <div role="alert" className="pj-status">
      <span>{conflict ? 'Changed in another window' : context.error ?? 'Not saved'}</span>
      <span className="pj-status-actions">{context.loadError ? <>
        <button type="button" className="btn" onClick={() => void retryProject()}>Retry</button>
        <button type="button" className="btn-ghost" onClick={showProjects}>Projects</button>
      </> : conflict ? <>
        <button type="button" className="btn" onClick={() => void reloadProject()}>Reload latest</button>
        <button type="button" className="btn-ghost" onClick={() => void keepProjectVersion()}>Keep mine</button>
      </> : <>
        <button type="button" className="btn" onClick={() => void retryProject()}>Retry</button>
        <button type="button" className="btn-ghost" onClick={() => void saveProject()}>Export</button>
      </>}</span>
    </div>
  );
}
