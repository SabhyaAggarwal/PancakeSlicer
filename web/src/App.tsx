import { lazy, Suspense, useEffect, useRef, useState } from 'react';
import { addInstance, arrangeAll, download, importFiles, newProject, openFileDialog, openProjectDialog, removeInstance, toolbarOperation, saveProject } from './actions';
import { CanvasToolbar, ManipulationPanel, SliceNotification, Toast } from './components/CanvasOverlays';
import { Sidebar } from './components/Sidebar';
import { TabBar, TopBar } from './components/TopBar';
const PlateChoice = lazy(() => import('./components/PlateChoice'));
const Plater3D = lazy(() => import('./plater/Plater3D'));
import { deleteAll, getState, removeSelected, setState, togglePrintable, useStore } from './state/store';

const PresetDialog = lazy(() => import('./components/Dialogs').then((m) => ({ default: m.PresetDialog })));
const FlushDialog = lazy(() => import('./components/FlushDialog').then((m) => ({ default: m.FlushDialog })));
const PreviewView = lazy(() => import('./components/PreviewView').then((m) => ({ default: m.PreviewView })));

export default function App() {
  const multiPlate = useStore((s) => s.projectPlates.length > 1);
  const dialog = useStore((s) => s.dialog);
  const flushDialog = useStore((s) => s.flushDialog);
  const tab = useStore((s) => s.tab);
  const loading = useStore((s) => s.loading);
  const jobDone = useStore((s) => s.job.state === 'done');
  const [dragOver, setDragOver] = useState(false);
  const previewOpened = useRef(false);
  if (tab === 'preview' || jobDone) previewOpened.current = true;

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement)?.closest('input,select,textarea') || getState().dialog || getState().flushDialog) return;
      const ctrl = e.ctrlKey || e.metaKey;
      if (ctrl && e.key.toLowerCase() === 'i') openFileDialog();
      else if (ctrl && e.key.toLowerCase() === 'o') openProjectDialog();
      else if (ctrl && e.key.toLowerCase() === 'n') newProject();
      else if (ctrl && e.key.toLowerCase() === 's') void saveProject(e.shiftKey);
      else if (ctrl && e.key.toLowerCase() === 'g') download();
      else if (ctrl && e.key === 'Delete') deleteAll();
      else if (ctrl) return;
      else if (e.key === 'Delete' || e.key === 'Backspace') removeSelected();
      else if (e.key.toLowerCase() === 'q') toolbarOperation('orient', e.shiftKey);
      else if (e.key === 'a' || e.key === 'A') arrangeAll();
      else if (e.key === '+' || e.key === '=') addInstance();
      else if (e.key === '-') removeInstance();
      else if (e.key === 'm' || e.key === 'M') setState((s) => ({ gizmo: s.gizmo === 'move' ? 'none' : 'move' }));
      else if (e.key === 'r' || e.key === 'R') setState((s) => ({ gizmo: s.gizmo === 'rotate' ? 'none' : 'rotate' }));
      else if (e.key === 's' || e.key === 'S') setState((s) => ({ gizmo: s.gizmo === 'scale' ? 'none' : 'scale' }));
      else if (e.key.toLowerCase() === 'v') { const selected = getState().selected; if (selected) togglePrintable(selected.object, selected.allInstances ? undefined : selected.instance); }
      else if (e.key === 'Escape') setState({ selected: null, gizmo: 'none' });
      else return;
      e.preventDefault();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  return (
    <div className="app">
      <TopBar />
      <TabBar />
      <div className="main">
        <div style={{ display: tab === 'prepare' ? 'contents' : 'none' }}>
          <Sidebar />
        </div>
        <div
          className={`canvas-area ${dragOver ? 'drag-over' : ''}`}
          onDragOver={(e) => {
            e.preventDefault();
            setDragOver(true);
          }}
          onDragLeave={() => setDragOver(false)}
          onDrop={(e) => {
            e.preventDefault();
            setDragOver(false);
            void importFiles(e.dataTransfer.files);
          }}
        >
          <div style={{ display: tab === 'prepare' ? 'contents' : 'none' }}>
            <Suspense fallback={<div role="status">Loading plater…</div>}><Plater3D /></Suspense>
            <CanvasToolbar />
            {multiPlate && <Suspense fallback={null}><PlateChoice /></Suspense>}
            <ManipulationPanel />
          </div>
          {previewOpened.current && (
            <div style={{ display: tab === 'preview' ? 'contents' : 'none' }}>
              <Suspense fallback={<div role="status">Loading preview…</div>}>
                <PreviewView />
              </Suspense>
            </div>
          )}
          <SliceNotification />
          {loading && <div className="loading">{loading}…</div>}
        </div>
      </div>
      <Suspense fallback={<div role="status">Loading dialog…</div>}>
        {dialog && <PresetDialog />}
        {flushDialog && <FlushDialog />}
      </Suspense>
      <Toast />
    </div>
  );
}
