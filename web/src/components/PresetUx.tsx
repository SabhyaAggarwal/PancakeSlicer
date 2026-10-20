import { lazy, Suspense } from 'react';
import { useStore } from '../state/store';

const SearchDialog = lazy(() => import('./SearchDialog'));
const SaveDialog = lazy(() => import('./PresetDialogs').then((m) => ({ default: m.SaveDialog })));
const UnsavedDialog = lazy(() => import('./PresetDialogs').then((m) => ({ default: m.UnsavedDialog })));
const CompareDialog = lazy(() => import('./PresetDialogs').then((m) => ({ default: m.CompareDialog })));
const ImportDialog = lazy(() => import('./PresetIO').then((m) => ({ default: m.ImportDialog })));
const ExportDialog = lazy(() => import('./PresetIO').then((m) => ({ default: m.ExportDialog })));

export default function PresetUx() {
  const ux = useStore((s) => s.presetUx);
  if (!ux) return null;
  return (
    <Suspense fallback={<div role="status">Loading…</div>}>
      {ux.kind === 'search' && <SearchDialog />}
      {ux.kind === 'save' && <SaveDialog key={`${ux.type}:${ux.slot}`} type={ux.type} slot={ux.slot} then={ux.then} />}
      {ux.kind === 'unsaved' && <UnsavedDialog type={ux.type} slot={ux.slot} target={ux.target} />}
      {ux.kind === 'compare' && <CompareDialog type={ux.type} left={ux.left} right={ux.right} />}
      {ux.kind === 'import' && <ImportDialog />}
      {ux.kind === 'export' && <ExportDialog />}
    </Suspense>
  );
}
