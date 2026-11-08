import { Suspense } from 'react';
import { useStore } from '../state/store';
import { preloadable } from '../preloadable';

const PresetsWindow = preloadable(() => import('../profiles/PresetsWindow'));
const ImportDialog = preloadable(() => import('./PresetIO').then((m) => ({ default: m.ImportDialog })));
const ExportDialog = preloadable(() => import('./PresetIO').then((m) => ({ default: m.ExportDialog })));

export default function PresetUx() {
  const ux = useStore((s) => s.presetUx);
  if (!ux) return null;
  return (
    <Suspense fallback={<div role="status">Loading…</div>}>
      {ux.kind === 'compare' && <PresetsWindow type={ux.type} name={ux.left} compare={ux.right ?? ''} />}
      {ux.kind === 'presets' && <PresetsWindow request={ux} type={ux.type} name={ux.name} edit={ux.edit} add={ux.add} stage={ux.stage}/>}
      {ux.kind === 'import' && <ImportDialog />}
      {ux.kind === 'export' && <ExportDialog />}
    </Suspense>
  );
}
