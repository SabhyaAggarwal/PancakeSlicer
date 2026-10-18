import { useEffect, useRef, useState } from 'react';
import { addInstance, arrangeAll, download, importFiles, init, newProject, openFileDialog, openProjectDialog, removeInstance } from './actions';
import { FlushDialog } from './components/FlushDialog';
import { CanvasToolbar, ManipulationPanel, SliceNotification, Toast } from './components/CanvasOverlays';
import { PresetDialog } from './components/Dialogs';
import { PreviewView } from './components/PreviewView';
import { Sidebar } from './components/Sidebar';
import { TabBar, TopBar } from './components/TopBar';
import Plater3D from './plater/Plater3D';
import { deleteAll, getState, removeSelected, setState, useStore } from './state/store';

export default function App() {
  const tab = useStore((s) => s.tab);
  const loading = useStore((s) => s.loading);
  const [dragOver, setDragOver] = useState(false);
  const previewOpened = useRef(false);
  if (tab === 'preview') previewOpened.current = true;

  useEffect(() => {
    void init();
    const onKey = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement)?.closest('input,select,textarea') || getState().dialog || getState().flushDialog) return;
      const ctrl = e.ctrlKey || e.metaKey;
      if (ctrl && e.key.toLowerCase() === 'i') openFileDialog();
      else if (ctrl && e.key.toLowerCase() === 'o') openProjectDialog();
      else if (ctrl && e.key.toLowerCase() === 'n') newProject();
      else if (ctrl && e.key.toLowerCase() === 'g') download();
      else if (ctrl && e.key === 'Delete') deleteAll();
      else if (ctrl) return;
      else if (e.key === 'Delete' || e.key === 'Backspace') removeSelected();
      else if (e.key === 'a' || e.key === 'A') arrangeAll();
      else if (e.key === '+' || e.key === '=') addInstance();
      else if (e.key === '-') removeInstance();
      else if (e.key === 'm' || e.key === 'M') setState((s) => ({ gizmo: s.gizmo === 'move' ? 'none' : 'move' }));
      else if (e.key === 'r' || e.key === 'R') setState((s) => ({ gizmo: s.gizmo === 'rotate' ? 'none' : 'rotate' }));
      else if (e.key === 's' || e.key === 'S') setState((s) => ({ gizmo: s.gizmo === 'scale' ? 'none' : 'scale' }));
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
            <Plater3D />
            <CanvasToolbar />
            <ManipulationPanel />
          </div>
          {previewOpened.current && (
            <div style={{ display: tab === 'preview' ? 'contents' : 'none' }}>
              <PreviewView />
            </div>
          )}
          <SliceNotification />
          {loading && <div className="loading">{loading}…</div>}
        </div>
      </div>
      <PresetDialog />
      <FlushDialog />
      <Toast />
    </div>
  );
}
