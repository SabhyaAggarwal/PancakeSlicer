import { lazy, Suspense, useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { arrangeAll, canExport, fileUse, STALE_RESULT_TIP, download, newProject, openFileDialog, openProjectDialog, outputKind, outsideVolumeWarning, saveProject, slice } from '../actions';
import { deleteAll, removeSelected, setState, useStore } from '../state/store';
import { canPaste, copySelection, pasteClipboard, selectAll } from '../edit';
import { historyVersion, redo, redoName, subscribeHistory, undo, undoName } from '../state/history';
import { CAMERA_VIEWS, VIEW_DIRECTIONS, cameraCommand, cameraSnapshot, subscribeCamera, perspective, autoPerspective } from '../camera';
import { Icon } from './Icon';

const About = lazy(() => import('./About'));

type MenuItem = { label: string; shortcut?: string; action?: () => void; disabled?: boolean; tip?: string; checked?: boolean; role?: 'menuitemradio' | 'menuitemcheckbox' } | 'sep';

function Menu({ title, items }: { title: string; items: MenuItem[] }) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => !ref.current?.contains(e.target as Node) && setOpen(false);
    window.addEventListener('mousedown', close);
    return () => window.removeEventListener('mousedown', close);
  }, [open]);
  return (
    <div className="menu" ref={ref}>
      <button className={`menu-title ${open ? 'open' : ''}`} onClick={() => setOpen(!open)}>
        {title}
      </button>
      {open && (
        <div className="menu-pop">
          {items.map((it, i) =>
            it === 'sep' ? (
              <div key={i} className="menu-sep" />
            ) : (
              <button
                key={it.label}
                className="menu-item"
                role={it.role}
                aria-checked={it.checked}
                disabled={it.disabled || !it.action}
                title={it.tip}
                onClick={() => {
                  setOpen(false);
                  it.action?.();
                }}
              >
                <span>{it.label}</span>
                {it.shortcut && <span className="menu-key">{it.shortcut}</span>}
              </button>
            ),
          )}
        </div>
      )}
    </div>
  );
}


export function TopBar() {
  const [aboutOpen, setAboutOpen] = useState(false);
  useSyncExternalStore(subscribeCamera, cameraSnapshot);
  useSyncExternalStore(subscribeHistory, historyVersion);
  const undoLabel = undoName(), redoLabel = redoName();
  const mod = /Mac/.test(navigator.platform) ? 'Cmd' : 'Ctrl';
  const exportEnabled = useStore(canExport);
  const stale = useStore((s) => s.job.stale);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const hasProjectObjects = useStore((s) => s.objects.length > 0 || s.projectPlates.some((p) => p.objects.length > 0));
  const hasSel = useStore((s) => !!s.selected);
  const pasteEnabled = useStore(canPaste);
  const mock = useStore((s) => s.api?.mock);
  const name = useStore((s) => (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf)$/i, ''));
  const kind = useStore(outputKind);
  const exportLabel = kind === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code';
  const exportTip = kind === 'gcode.3mf' ? 'Export current sliced file' : 'Export current plate as G-code';
  return (
    <div className="topbar">
      <Icon name="OrcaSlicer" size={20} className="topbar-logo" />
      <Menu
        title="File"
        items={[
          { label: 'New Project', shortcut: 'Ctrl+N', action: newProject, tip: 'Start a new project' },
          { label: 'Save Project', shortcut: 'Ctrl+S', action: () => void saveProject(), disabled: !hasProjectObjects, tip: 'Save current project to file' },
          { label: 'Save Project as...', shortcut: 'Ctrl+Shift+S', action: () => void saveProject(true), disabled: !hasProjectObjects, tip: 'Save current project as' },
          { label: 'Open Project...', shortcut: 'Ctrl+O', action: openProjectDialog, tip: 'Open a project file' },
          { label: 'Recent projects...', action: () => setState({ sessionDialog: 'recent' }) },
          'sep',
          { label: 'Import 3MF/STL/STEP/SVG/OBJ/AMF...', shortcut: 'Ctrl+I', action: openFileDialog, tip: 'Load a model' },
          'sep',
          { label: exportLabel + '...', shortcut: 'Ctrl+G', action: download, disabled: !exportEnabled, tip: stale ? STALE_RESULT_TIP : exportTip },
          { label: 'Export all objects as one STL...', tip: 'Export all objects as one STL' },
          { label: 'Export all objects as STLs...', tip: 'Export all objects as STLs' },
          { label: 'Export toolpaths as OBJ...', tip: 'Export toolpaths as OBJ' },
          { label: 'Export Preset Bundle...', tip: 'Export current configuration to files' },
          'sep',
          { label: 'Quit', tip: 'Quit' },
        ]}
      />
      <Menu
        title="Edit"
        items={[
          { label: undoLabel ? `Undo ${undoLabel}` : 'Undo', shortcut: `${mod}+Z`, action: undo, disabled: !undoLabel },
          { label: redoLabel ? `Redo ${redoLabel}` : 'Redo', shortcut: `${mod}+Y`, action: redo, disabled: !redoLabel },
          'sep',
          { label: 'Copy', shortcut: 'Ctrl+C', action: copySelection, disabled: !hasSel, tip: 'Copy selection to clipboard' },
          { label: 'Paste', shortcut: 'Ctrl+V', action: pasteClipboard, disabled: !pasteEnabled, tip: 'Paste clipboard' },
          { label: 'Delete selected', shortcut: 'Del', action: removeSelected, disabled: !hasSel, tip: 'Deletes the current selection' },
          { label: 'Delete all', shortcut: 'Ctrl+Del', action: deleteAll, disabled: !hasObjects, tip: 'Deletes all objects' },
          'sep',
          { label: 'Select All', shortcut: 'Ctrl+A', action: selectAll, disabled: !hasObjects, tip: 'Selects all objects' },
          { label: 'Deselect All', shortcut: 'Esc', action: () => setState({ selected: null }), disabled: !hasSel, tip: 'Deselects all objects' },
          'sep',
          { label: 'Arrange', shortcut: 'A', action: arrangeAll, disabled: !hasObjects },
          'sep',
          { label: 'Preferences', shortcut: 'Ctrl+P' },
        ]}
      />
      <Menu
        title="View"
        items={[
          ...CAMERA_VIEWS.map((label, i) => ({ label, shortcut: `${/Mac/.test(navigator.platform) ? 'Cmd' : 'Ctrl'}+${i}`, action: () => cameraCommand(VIEW_DIRECTIONS[i]), tip: i ? `${label} View` : label })),
          'sep',
          { label: 'Use Perspective View', role: 'menuitemradio', checked: perspective, action: () => cameraCommand('perspective') },
          { label: 'Use Orthogonal View', role: 'menuitemradio', checked: !perspective, action: () => cameraCommand('orthogonal') },
          { label: 'Auto Perspective', role: 'menuitemcheckbox', checked: autoPerspective, action: () => cameraCommand('auto') },
        ]}
      />
      <Menu
        title="Help"
        items={[
          { label: 'Keyboard Shortcuts', shortcut: 'Shift+?', tip: 'Show the list of keyboard shortcuts' },
          { label: 'Setup Wizard', tip: 'Setup Wizard' },
          'sep',
          { label: 'Show Configuration Folder', tip: 'Show Configuration Folder' },
          { label: 'Troubleshoot Center' },
          { label: 'Show Tip of the Day', tip: 'Show Tip of the Day' },
          { label: 'Check for Updates', tip: 'Check for Updates' },
          'sep',
          { label: 'About OrcaSlicer', action: () => setAboutOpen(true), tip: 'Version, licence and Corresponding Source' },
        ]}
      />
      <button type="button" className="icon-btn topbar-btn" title={undoLabel ? `Undo ${undoLabel} (${mod}+Z)` : 'Undo'} aria-label="Undo" disabled={!undoLabel} onClick={undo}>
        <Icon name={undoLabel ? 'topbar_undo' : 'topbar_undo_inactive'} size={20} />
      </button>
      <button type="button" className="icon-btn topbar-btn" title={redoLabel ? `Redo ${redoLabel} (${mod}+Y)` : 'Redo'} aria-label="Redo" disabled={!redoLabel} onClick={redo}>
        <Icon name={redoLabel ? 'topbar_redo' : 'topbar_redo_inactive'} size={20} />
      </button>
      {aboutOpen && <Suspense fallback={<div role="status">Loading About…</div>}><About close={() => setAboutOpen(false)} /></Suspense>}
      <div className="topbar-title">
        {name ?? 'Untitled'}
        {mock && <span className="mock-badge" title="The slicing service did not answer; jobs are simulated">mock</span>}
      </div>
    </div>
  );
}

export function TabBar() {
  const tab = useStore((s) => s.tab);
  const job = useStore((s) => s.job);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const outside = useStore(outsideVolumeWarning);
  const busy = job.state === 'running' || job.state === 'submitting';
  const exportEnabled = useStore(canExport);
  const exportLabel = useStore(outputKind) === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code file';
  return (
    <div className="tabbar">
      <div className="tabbar-tabs">
        <button className={`main-tab ${tab === 'prepare' ? 'active' : ''}`} onClick={() => setState({ tab: 'prepare' })}>
          <Icon name="tab_3d_active" size={20} />
          <span>Prepare</span>
        </button>
        <button className={`main-tab ${tab === 'preview' ? 'active' : ''}`} onClick={() => setState({ tab: 'preview' })}>
          <Icon name="tab_preview_active" size={20} />
          <span>Preview</span>
        </button>
      </div>
      <div className="tabbar-actions">
        <button className="side-btn" disabled={!hasObjects || busy || !!outside} title={outside ?? (hasObjects ? 'Slice the plate into a printable file and show it in Preview' : 'Add a model to the plate first')} onClick={() => void slice()}>
          Slice plate
        </button>
        <button className="side-btn" disabled={!exportEnabled} onClick={download} title={job.stale ? STALE_RESULT_TIP : outside ?? (exportEnabled ? `Download the sliced file. ${fileUse()}` : 'Slice the plate first')}>
          {exportLabel}
        </button>
      </div>
    </div>
  );
}
