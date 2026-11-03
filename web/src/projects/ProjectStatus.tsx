import { saveProject } from '../actions';
import { NoticeLog } from '../components/NoticeLog';
import { keepProjectVersion, reloadProject, retryProject, showProjects, useProject } from '../flow/project';
import { projectsHref } from '../flow/routes';

export function ProjectStatus() {
  return <><LifecycleStatus /><NoticeLog /></>;
}

function LifecycleStatus() {
  const { value, context } = useProject();
  if (value === 'missing') return (
    <div className="projects" role="status"><p>Project not found</p>
      <a href={projectsHref()} onClick={(e) => { e.preventDefault(); showProjects(); }}>Back to projects</a>
    </div>
  );
  if (value === 'opening') return <div className="projects" role="status">Opening project…</div>;
  if (value !== 'failed' && !context.conflict) return null;
  return (
    <div role="alert" className="notice">
      <span>{context.error ?? 'This project changed in another tab. Reload its latest version or keep this version.'}</span>{' '}
      {context.loadError ? <>
        <button type="button" onClick={() => void retryProject()}>Retry</button>{' '}
        <button type="button" onClick={showProjects}>Back to projects</button>
      </> : context.conflict || /another tab/.test(context.error ?? '') ? <>
        <button type="button" onClick={() => void reloadProject()}>Reload latest</button>{' '}
        <button type="button" onClick={() => void keepProjectVersion()}>Keep this version</button>
      </> : <>
        <button type="button" onClick={showProjects}>Delete old projects</button>{' '}
        <button type="button" onClick={() => void saveProject()}>Export</button>{' '}
        <button type="button" onClick={() => void retryProject()}>Retry</button>
      </>}
    </div>
  );
}
