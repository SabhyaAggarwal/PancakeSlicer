import { lazy, Suspense, useEffect, useRef, useState } from 'react';
import { togglePaint } from './plater/paint';
import { addInstance, arrangeAll, download, importFiles, newProject, openFileDialog, openProjectDialog, removeInstance, slice, toolbarOperation, saveProject } from './actions';
import { toggleCut } from './plater/cut';
import { CanvasToolbar, CutPanel, ManipulationPanel, PaintPanel, SliceNotification, Toast } from './components/CanvasOverlays';
import { LayerHeightBar, LayerHeightPanel } from './components/LayerHeight';
import { Sidebar } from './components/Sidebar';
import { AskHost } from './components/Ask';
import { TabBar, TopBar } from './components/TopBar';
const PlateChoice = lazy(() => import('./components/PlateChoice'));
const EmptyPlate = lazy(() => import('./components/FirstRun'));
const Plater3D = lazy(() => import('./plater/Plater3D'));
import { deleteAll, getState, removeSelected, setState, togglePrintable, updateInstance, useStore } from './state/store';
import { ownsKeys } from './keys';
import { copySelection, pasteClipboard, selectAll, startRename } from './edit';
const ContextMenu = lazy(() => import('./components/ContextMenu'));
const MeasurePanel = lazy(() => import('./components/MeasurePanel'));
import { dropLastFeature, setMeasure } from './plater/measureState';
import { redo, undo, undoable } from './state/history';

const PresetDialog = lazy(() => import('./components/Dialogs').then((m) => ({ default: m.PresetDialog })));
const PresetUx = lazy(() => import('./components/PresetUx'));
const FlushDialog = lazy(() => import('./components/FlushDialog').then((m) => ({ default: m.FlushDialog })));
const PreviewView = lazy(() => import('./components/PreviewView').then((m) => ({ default: m.PreviewView })));
const SessionDialog = lazy(() => import('./components/SessionDialog'));
const AppDialog = lazy(() => import('./components/AppDialogs'));
const CalibDialog = lazy(() => import('./components/CalibDialog'));
const NonplanarCalibDialog = lazy(() => import('./components/NonplanarCalibDialog'));

function nudge(key: string, fine: boolean) {
  const s = getState();
  const step = fine ? 1 : 10;
  const d = ({ ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, step], ArrowDown: [0, -step] } as Record<string, number[]>)[key];
  const turn = key === 'PageUp' ? 45 : key === 'PageDown' ? -45 : 0;
  if (!d && !turn) return false;
  const targets = s.objects.filter((o) => !o.project?.placeholder).flatMap((o) => o.instances
    .filter((i) => s.selected?.all || (o.id === s.selected?.object && (s.selected.allInstances || i.id === s.selected.instance)))
    .map((i) => [o.id, i] as const));
  if (!targets.length) return false;
  undoable(d ? 'Move Object' : 'Rotate Object', () => targets.forEach(([o, i]) =>
    updateInstance(o, i.id, d ? { x: i.x + d[0], y: i.y + d[1] } : { rotZ: i.rotZ + turn })), d ? 'nudge-move' : 'nudge-rotate');
  return true;
}

export default function App() {
  const multiPlate = useStore((s) => s.projectPlates.length > 1);
  const dialog = useStore((s) => s.dialog);
  const flushDialog = useStore((s) => s.flushDialog);
  const presetUx = useStore((s) => !!s.presetUx);
  const appDialog = useStore((s) => s.appDialog);
  const calibDialog = useStore((s) => s.calibDialog);
  const contextMenu = useStore((s) => !!s.contextMenu);
  const sessionDialog = useStore((s) => s.sessionDialog);
  const tab = useStore((s) => s.tab);
  const gizmo = useStore((s) => s.gizmo);
  const loading = useStore((s) => s.loading);
  const jobDone = useStore((s) => s.job.state === 'done');
  const empty = useStore((s) => !s.objects.length && !s.project && !!s.printer);
  const [dragOver, setDragOver] = useState(false);
  const previewOpened = useRef(false);
  if (tab === 'preview' || jobDone) previewOpened.current = true;

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey || e.shiftKey || e.key.toLowerCase() !== 'f') return;
      e.preventDefault();
      e.stopPropagation();
      setState({ presetUx: { kind: 'search' } });
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, []);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (ownsKeys(e) || getState().dialog || getState().flushDialog || getState().appDialog || getState().calibDialog || getState().sessionDialog) return;
      const ctrl = e.ctrlKey || e.metaKey;
      const prepare = getState().tab === 'prepare';
      if (ctrl && e.key.toLowerCase() === 'z') e.shiftKey ? redo() : undo();
      else if (ctrl && e.key.toLowerCase() === 'y') redo();
      else if (ctrl && e.key.toLowerCase() === 'p') setState({ appDialog: 'preferences' });
      else if (e.key === '?') setState({ appDialog: 'shortcuts' });
      else if (ctrl && e.key.toLowerCase() === 'r') { if (getState().objects.length && getState().job.state !== 'running') void slice(); }
      else if (ctrl && e.key.toLowerCase() === 'd') deleteAll();
      else if (ctrl && e.key.toLowerCase() === 'i') openFileDialog();
      else if (ctrl && e.key.toLowerCase() === 'o') openProjectDialog();
      else if (ctrl && e.key.toLowerCase() === 'n') newProject();
      else if (ctrl && e.key.toLowerCase() === 's') void saveProject(e.shiftKey);
      else if (ctrl && e.key.toLowerCase() === 'g') download();
      else if (ctrl && e.key === 'Delete') deleteAll();
      else if (ctrl && e.key.toLowerCase() === 'a' && !e.shiftKey) selectAll();
      else if (ctrl && e.key.toLowerCase() === 'c' && !window.getSelection()?.toString()) copySelection();
      else if (ctrl && e.key.toLowerCase() === 'v') pasteClipboard();
      else if (ctrl) return;
      else if (getState().gizmo === 'measure' && (e.key === 'Delete' || e.key === 'Backspace')) setMeasure({});
      else if (getState().gizmo === 'measure' && e.key === 'Escape') { if (!dropLastFeature()) setState({ gizmo: 'none' }); }
      else if (e.key === 'Delete' || e.key === 'Backspace') removeSelected();
      else if (e.key.toLowerCase() === 'q') toolbarOperation('orient', e.shiftKey);
      else if (e.key === 'a' || e.key === 'A') arrangeAll(e.key === 'a');
      else if (e.key === '+' || e.key === '=') addInstance();
      else if (e.key === '-') removeInstance();
      else if (e.key === 'm' || e.key === 'M') setState((s) => ({ gizmo: s.gizmo === 'move' ? 'none' : 'move' }));
      else if (e.key === 'r' || e.key === 'R') setState((s) => ({ gizmo: s.gizmo === 'rotate' ? 'none' : 'rotate' }));
      else if (e.key === 's' || e.key === 'S') setState((s) => ({ gizmo: s.gizmo === 'scale' ? 'none' : 'scale' }));
      else if (e.key === 'f' || e.key === 'F') setState((s) => ({ gizmo: s.gizmo === 'flatten' || !s.selected || s.selected.all ? 'none' : 'flatten' }));
      else if (e.key === 'c' || e.key === 'C') toggleCut();
      else if (e.key === 'u' || e.key === 'U') setState((s) => ({ gizmo: s.gizmo === 'measure' || !s.selected ? 'none' : 'measure' }));
      else if (e.key.toLowerCase() === 'l') togglePaint('support');
      else if (e.key.toLowerCase() === 'p') togglePaint('seam');
      else if (e.key.toLowerCase() === 'h') togglePaint('fuzzy');
      else if (e.key === 'F2') startRename();
      else if (e.key.toLowerCase() === 'v') { const selected = getState().selected; if (selected) togglePrintable(selected.object, selected.allInstances ? undefined : selected.instance); }
      else if (prepare && !e.altKey && nudge(e.key, e.shiftKey)) {  }
      else if (e.key === 'Escape') setState({ selected: null, gizmo: 'none' });
      else return;
      e.preventDefault();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  useEffect(() => {
    let depth = 0;
    const files = (e: DragEvent) => !!e.dataTransfer?.types.includes('Files') && !document.querySelector('dialog[open]');
    const enter = (e: DragEvent) => { if (files(e)) { depth++; setDragOver(true); } };
    const leave = (e: DragEvent) => { if (files(e) && --depth <= 0) { depth = 0; setDragOver(false); } };
    const over = (e: DragEvent) => { if (files(e)) e.preventDefault(); };
    const drop = (e: DragEvent) => {
      depth = 0;
      setDragOver(false);
      if (!files(e) || e.defaultPrevented) return;
      e.preventDefault();
      void importFiles(e.dataTransfer!.files);
    };
    const events = { dragenter: enter, dragleave: leave, dragover: over, drop } as const;
    for (const [k, f] of Object.entries(events)) window.addEventListener(k, f as EventListener);
    return () => { for (const [k, f] of Object.entries(events)) window.removeEventListener(k, f as EventListener); };
  }, []);

  return (
    <div className="app">
      <TopBar />
      <TabBar />
      <div className="main">
        <div style={{ display: tab === 'prepare' ? 'contents' : 'none' }}>
          <Sidebar />
        </div>
        <div className={`canvas-area ${dragOver ? 'drag-over' : ''}`}>
          <div style={{ display: tab === 'prepare' ? 'contents' : 'none' }}>
            <Suspense fallback={<div role="status">Loading plater…</div>}><Plater3D /></Suspense>
            <CanvasToolbar />
            <ManipulationPanel />
            <CutPanel />
            <PaintPanel />
            <LayerHeightPanel />
            <LayerHeightBar />
            {gizmo === 'measure' && <Suspense fallback={null}><MeasurePanel /></Suspense>}
            {empty && !loading && <Suspense fallback={null}><EmptyPlate /></Suspense>}
          </div>
          {previewOpened.current && (
            <div style={{ display: tab === 'preview' ? 'contents' : 'none' }}>
              <Suspense fallback={<div role="status">Loading preview…</div>}>
                <PreviewView />
              </Suspense>
            </div>
          )}
          {multiPlate && <Suspense fallback={null}><PlateChoice /></Suspense>}
          <SliceNotification />
          {loading && <div className="loading">{loading}…</div>}
        </div>
      </div>
      <Suspense fallback={<div role="status">Loading dialog…</div>}>
        {dialog && <PresetDialog />}
        {flushDialog && <FlushDialog />}
        {presetUx && <PresetUx />}
        {appDialog && <AppDialog which={appDialog} />}
        {calibDialog === 'nonplanar' ? <NonplanarCalibDialog /> : calibDialog && <CalibDialog which={calibDialog} />}
        {contextMenu && <ContextMenu />}
        {sessionDialog && <SessionDialog />}
      </Suspense>
      <AskHost />
      <Toast />
    </div>
  );
}
