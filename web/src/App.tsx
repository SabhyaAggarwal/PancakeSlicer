import { Modal, ModalTitle } from './components/Modal';
import { openPalette } from './commands/registry';
import { lazy, Suspense, useEffect, useRef, useState } from 'react';
import { isPaint, paintKeyTool, setPaintTool, togglePaint, type PaintKind } from './plater/paint';
import { toggleViewOption } from './plater/viewOptions';
import { addInstance, arrangeAll, download, importFiles, newProject, openFileDialog, openProjectDialog, removeInstance, slice, toolbarOperation, saveProject, toggleBoolean, toggleText } from './actions';
import { setCut, toggleCut } from './plater/cut';
import { CanvasToolbar, CutPanel, PaintPanel, SliceNotification, Toast } from './components/CanvasOverlays';
import { LoadingState } from './components/Notices';
import { LayerHeightBar, LayerHeightPanel } from './components/LayerHeight';
import { Sidebar } from './components/Sidebar';
import { AskHost } from './components/Ask';
import { TopBar } from './components/TopBar';
const ControlHints = lazy(() => import('./components/ControlHints').then(m => ({default: m.ControlHints})));
const BrimEarsPanel = lazy(() => import('./components/BrimEarsPanel'));
const TransformPanel = lazy(() => import('./components/Transform').then(m => ({ default: m.TransformPanel })));
const ObjectsOutliner = lazy(() => import('./components/ObjectsOutliner'));
const ObjectInfo = lazy(() => import('./components/ObjectInfo'));
const BooleanPanel = lazy(() => import('./components/BooleanPanel'));
const TextPanel = lazy(() => import('./components/TextPanel'));
const SvgPanel = lazy(() => import('./components/SvgPanel'));
const AssemblyBar = lazy(() => import('./components/AssemblyBar'));
const CanvasTools = lazy(() => import('./components/CanvasTools'));
const Navigator = lazy(() => import('./components/Navigator').then((m) => ({ default: m.Navigator })));
const PlateChoice = lazy(() => import('./components/PlateChoice'));
const EmptyPlate = lazy(() => import('./components/FirstRun'));
const WorkspaceRail = lazy(() => import('./components/WorkspaceRail'));
const SavedPreferencesNotice = lazy(() => import('./components/SavedPreferencesNotice'));
const ProjectStatus = lazy(() => import('./projects/ProjectStatus').then((m) => ({ default: m.ProjectStatus })));
const Plater3D = lazy(() => import('./plater/Plater3D'));
import { deleteAll, getState, isSelected, removeSelected, setState, subscribeStore, togglePrintable, updateInstance, useStore } from './state/store';
import { ownsKeys } from './keys';
import { copySelection, cutSelection, pasteClipboard, selectAll, startRename } from './edit';
const ContextMenu = lazy(() => import('./components/ContextMenu'));
const MeasurePanel = lazy(() => import('./components/MeasurePanel'));
import { dropLastFeature, setMeasure } from './plater/measureState';
import { redo, undo, undoable } from './state/history';
import { assembly, setAssembly, useAssembly } from './plater/assembly';
import { toggleSidebar, useSidebarHidden } from './sidebar';

const PresetDialog = lazy(() => import('./components/Dialogs').then((m) => ({ default: m.PresetDialog })));
const PresetUx = lazy(() => import('./components/PresetUx'));
const FlushDialog = lazy(() => import('./components/FlushDialog').then((m) => ({ default: m.FlushDialog })));
const PreviewView = lazy(() => import('./components/PreviewView').then((m) => ({ default: m.PreviewView })));
const ProjectsView = lazy(() => import('./projects/ProjectsView'));
const Palette = lazy(() => import('./commands/Palette'));

import { useSyncExternalStore } from 'react';
import { paletteQuery, subscribePalette } from './commands/registry';
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
    .filter((i) => isSelected(s.selected, o.id, i.id))
    .map((i) => [o.id, i] as const));
  if (!targets.length) return false;
  undoable(d ? 'Move Object' : 'Rotate Object', () => targets.forEach(([o, i]) =>
    updateInstance(o, i.id, d ? { x: i.x + d[0], y: i.y + d[1] } : { rotZ: i.rotZ + turn })), d ? 'nudge-move' : 'nudge-rotate');
  return true;
}

const visibleCanvas = () => [...document.querySelectorAll<HTMLCanvasElement>('.canvas-area canvas')].find((c) => c.offsetParent !== null && c.clientWidth > 0);

function focusSoon(find: () => HTMLElement | null | undefined, frames = 60) {
  const idle = () => !document.activeElement || document.activeElement === document.body || document.activeElement instanceof HTMLCanvasElement && document.activeElement.offsetParent === null;
  const step = (left: number) => {
    if (!idle()) return;
    const el = find();
    if (el) {
      if (el instanceof HTMLCanvasElement && !el.hasAttribute('tabindex')) el.tabIndex = -1;
      const ring = !(el instanceof HTMLCanvasElement);
      el.focus({ preventScroll: true, focusVisible: ring } as FocusOptions);
      if (ring) { el.dataset.focusRing = ''; el.addEventListener('blur', () => delete el.dataset.focusRing, { once: true }); }
    } else if (left > 0) requestAnimationFrame(() => step(left - 1));
  };
  requestAnimationFrame(() => step(frames));
}

export default function App() {
  const projects = useStore((s) => s.tab === 'projects');
  const palette = useSyncExternalStore(subscribePalette, paletteQuery);
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
  const empty = useStore((s) => !s.objects.length && !s.project && s.projectPlates.length < 2);
  const assemblyOn = useAssembly().on;
  const sidebarHidden = useSidebarHidden();
  const [dragOver, setDragOver] = useState(false);
  const previewOpened = useRef(false);
  if (tab === 'preview' || jobDone) previewOpened.current = true;

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey || e.shiftKey || e.key.toLowerCase() !== 'f') return;
      e.preventDefault();
      e.stopPropagation();
      openPalette('/');
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
      else if (ctrl && e.key.toLowerCase() === 'a' && !e.shiftKey && getState().cut?.editing) { const c = getState().cut!; setCut({ selectedConnectors: (c.connectors ?? []).map((_, i) => i) }); }
      else if (ctrl && e.key.toLowerCase() === 'a' && !e.shiftKey) selectAll();
      else if (ctrl && e.shiftKey && e.key === 'Enter') toggleViewOption('wireframe');
      else if (ctrl && e.key.toLowerCase() === 'e' && getState().features.brim_ears && getState().tab === 'prepare') toggleViewOption('labels');
      else if (ctrl && e.key.toLowerCase() === 'c' && !window.getSelection()?.toString()) copySelection();
      else if (ctrl && e.key.toLowerCase() === 'x' && !window.getSelection()?.toString() && prepare) cutSelection();
      else if (ctrl && e.key === '\\') toggleSidebar();
      else if (ctrl && e.key.toLowerCase() === 'k') { const s = getState(); if (s.selected && !s.project) setState({ appDialog: 'clone' }); }
      else if (ctrl) return;
      else if (e.key === 'Tab' && !e.shiftKey && !e.altKey && document.activeElement instanceof HTMLCanvasElement) {
        e.preventDefault();
        setState((s) => ({ tab: s.tab === 'prepare' ? 'preview' : 'prepare' }));
        focusSoon(visibleCanvas);
      }
      else if (getState().gizmo === 'measure' && (e.key === 'Delete' || e.key === 'Backspace')) setMeasure({});
      else if (getState().gizmo === 'measure' && e.key === 'Escape') { if (!dropLastFeature()) setState({ gizmo: 'none' }); }
      else if (e.key === 'Delete' || e.key === 'Backspace') removeSelected();
      else if (isPaint(getState().gizmo) && !e.altKey && paintKeyTool(getState().gizmo as PaintKind, e.key)) setPaintTool({ tool: paintKeyTool(getState().gizmo as PaintKind, e.key)! });
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
      else if (e.key.toLowerCase() === 'n') togglePaint('color');
      else if (e.key.toLowerCase() === 'e' && getState().tab === 'prepare') {
        const s = getState(), o = s.objects.find((x) => x.id === s.selected?.object);
        if (!s.features.brim_ears) toggleViewOption('labels');
        else if (o && !o.native && !o.volumes?.length) setState({ gizmo: s.gizmo === 'brimears' ? 'none' : 'brimears' });
      }
      else if (e.key.toLowerCase() === 'b' && !e.ctrlKey && !e.metaKey) toggleBoolean();
      else if (e.key.toLowerCase() === 't' && !e.ctrlKey && !e.metaKey) toggleText();
      else if (getState().gizmo === 'color' && /^[1-9]$/.test(e.key)) { if (Number(e.key) <= getState().filamentSlots.length) setPaintTool({ filament: Number(e.key) }); }
      else if (e.key === 'F2') startRename();
      else if (e.key.toLowerCase() === 'v') { const selected = getState().selected; if (selected) togglePrintable(selected.object, selected.allInstances ? undefined : selected.instance); }
      else if (prepare && !e.altKey && nudge(e.key, e.shiftKey)) {  }
      else if (e.key === 'Escape') {
        const s = getState();
        if (!s.selected && s.gizmo === 'none' && !assembly().on && document.activeElement instanceof HTMLCanvasElement) document.activeElement.blur();
        if (assembly().on) setAssembly({ on: false });
        if (s.gizmo === 'move' || s.gizmo === 'rotate' || s.gizmo === 'scale') setState({gizmo: 'none'});
        else setState({ selected: null, gizmo: 'none' });
      }
      else return;
      e.preventDefault();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  useEffect(() => {
    const down = (e: PointerEvent) => {
      if (!(e.target instanceof HTMLCanvasElement)) return;
      window.getSelection()?.removeAllRanges();
      if (!e.target.hasAttribute('tabindex')) e.target.tabIndex = -1;
      e.target.focus({ preventScroll: true });
    };
    window.addEventListener('pointerdown', down, true);
    return () => window.removeEventListener('pointerdown', down, true);
  }, []);

  useEffect(() => {
    let prev = getState();
    const off = subscribeStore(() => {
      const s = getState(), was = prev;
      prev = s;
      if (document.activeElement && document.activeElement !== document.body) return;
      if (s.objects.length > was.objects.length) focusSoon(() => {
        const rows = [...document.querySelectorAll<HTMLElement>('.ol-row:not(.ol-child):not(.ol-child2)')];
        return rows.find((r) => r.classList.contains('selected')) ?? rows[rows.length - 1];
      });
      else if (s.job.state === 'done' && was.job.state !== 'done') focusSoon(() =>
        [...document.querySelectorAll<HTMLButtonElement>('.tabbar-actions button:not(:disabled)')].find((b) => /^Export/.test(b.textContent ?? '')));
    });
    return () => { off(); };
  }, []);

  useEffect(() => {
    const onPaste = (e: ClipboardEvent) => {
      const s = getState();
      if (e.target instanceof Element && e.target.closest('input,select,textarea,[contenteditable="true"],dialog')) return;
      if (s.dialog || s.flushDialog || s.appDialog || s.calibDialog || s.sessionDialog) return;
      const files = [...(e.clipboardData?.files ?? [])].filter((f) => /\.(stl|obj|3mf|step|stp)$/i.test(f.name));
      e.preventDefault();
      if (files.length) void importFiles(files);
      else pasteClipboard();
    };
    window.addEventListener('paste', onPaste);
    return () => window.removeEventListener('paste', onPaste);
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
      <Suspense fallback={null}><SavedPreferencesNotice /></Suspense>
      {projects && <Suspense fallback={null}><ProjectsView /></Suspense>}
      <div style={{ display: projects ? 'none' : undefined }} className={`main ${sidebarHidden ? 'sidebar-hidden' : ''} ${dragOver ? 'drag-over' : ''}`}>
        <Suspense fallback={null}><WorkspaceRail><Sidebar /></WorkspaceRail></Suspense>
        <div className="canvas-area" data-viewport={tab}>
          {!empty && <Suspense fallback={null}><ObjectsOutliner/></Suspense>}
          {sidebarHidden && (
            <button type="button" className="btn sidebar-reveal" aria-expanded={false} onClick={() => toggleSidebar(false)}>Settings</button>
          )}
          <div style={{ display: tab === 'prepare' ? 'contents' : 'none' }}>
            <Suspense fallback={null}><Plater3D /></Suspense>
            <CanvasToolbar />
            {!projects && tab === 'prepare' && (gizmo === 'move' || gizmo === 'rotate' || gizmo === 'scale') && <Suspense fallback={null}><TransformPanel /></Suspense>}
            <CutPanel />
            <PaintPanel />
            <LayerHeightPanel />
            <LayerHeightBar />
            {gizmo === 'measure' && <Suspense fallback={null}><MeasurePanel /></Suspense>}
            {!empty && <Suspense fallback={null}><CanvasTools /></Suspense>}
            {!empty && <Suspense fallback={null}><ObjectInfo /></Suspense>}
            {gizmo === 'brimears' && <Suspense fallback={null}><BrimEarsPanel /></Suspense>}
            {gizmo === 'boolean' && <Suspense fallback={null}><BooleanPanel /></Suspense>}
            {gizmo === 'text' && <Suspense fallback={null}><TextPanel /></Suspense>}
            {gizmo === 'svg' && <Suspense fallback={null}><SvgPanel /></Suspense>}
            {assemblyOn && <Suspense fallback={null}><AssemblyBar /></Suspense>}
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
          <Suspense fallback={null}><Navigator /></Suspense>
          <Suspense fallback={null}><ProjectStatus /></Suspense>
          <SliceNotification />
          {loading && <LoadingState text={loading} />}
        </div>
      </div>
      <Suspense fallback={<div role="status">Loading dialog…</div>}>
        {dialog && <PresetDialog />}
        {flushDialog && <FlushDialog />}
        {presetUx && <PresetUx />}
        {appDialog && <AppDialog which={appDialog} />}
        {calibDialog === 'nonplanar' ? <NonplanarCalibDialog /> : calibDialog && <CalibDialog which={calibDialog} />}
        {contextMenu && <ContextMenu />}
        {sessionDialog && <Modal label="Projects" onClose={() => setState({ sessionDialog: null })}><ModalTitle title="Projects" onClose={() => setState({ sessionDialog: null })} /><ProjectsView /></Modal>}
        {palette !== null && <Palette />}
      </Suspense>
      <Suspense fallback={null}><ControlHints /></Suspense>
      <AskHost />
      <Toast />
    </div>
  );
}
