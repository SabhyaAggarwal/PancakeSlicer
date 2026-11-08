import { useStore } from '../state/store';
import { useViewer, useViewerProgress } from '../flow/viewer';
import { LoadingState } from './Notices';
import { joinParts } from '../separator';

export function PreviewLoading() {
  const viewer = useViewer();
  const progress = useViewerProgress();
  const tab = useStore((s) => s.tab);
  const jobKey = useStore((s) => (s.job.state === 'done' && !s.job.stale && s.job.id ? `job:${s.job.id}` : null));
  const id = viewer.context.source?.id;
  if (viewer.value !== 'loading' || !id) return null;
  if (!(tab === 'preview' || (tab === 'prepare' && jobKey && (id === jobKey || id === `${jobKey}:extended`)))) return null;
  const p = progress?.id === id ? progress : null;
  if (p && p.total && p.loaded >= p.total) return <LoadingState text="Preparing preview" />;
  if (!p?.total) return <LoadingState text="Loading preview" />;
  return <LoadingState text={`${joinParts(['Loading preview', `${(p.bytes / 1e6).toFixed(1)} MB`])} ${Math.floor((100 * p.loaded) / p.total)}%`} />;
}
