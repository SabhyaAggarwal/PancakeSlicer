import { useEffect, useRef, useState } from 'react';
import { arrangeAll, download, newProject, openFileDialog, openProjectDialog, outputKind, slice } from '../actions';
import { deleteAll, removeSelected, setState, useStore } from '../state/store';
import { Icon } from './Icon';

type MenuItem = { label: string; shortcut?: string; action?: () => void; disabled?: boolean; tip?: string } | 'sep';

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

const view = (key: string) => window.dispatchEvent(new KeyboardEvent('keydown', { key }));

export function TopBar() {
  const jobDone = useStore((s) => s.job.state === 'done');
  const hasObjects = useStore((s) => s.objects.length > 0);
  const hasSel = useStore((s) => !!s.selected);
  const mock = useStore((s) => s.api?.mock);
  const name = useStore((s) => (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf)$/i, ''));
  const project = useStore((s) => !!s.project);
  const exportLabel = outputKind() === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code';
  return (
    <div className="topbar">
      <Icon name="OrcaSlicer" size={20} className="topbar-logo" />
      <Menu
        title="File"
        items={[
          { label: 'New Project', shortcut: 'Ctrl+N', action: newProject, tip: 'Start a new project' },
          { label: 'Open Project...', shortcut: 'Ctrl+O', action: openProjectDialog, tip: 'Open a project file' },
          'sep',
          { label: 'Import 3MF/STL/STEP/SVG/OBJ/AMF...', shortcut: 'Ctrl+I', action: openFileDialog, tip: 'Load a model' },
          'sep',
          { label: exportLabel + '...', shortcut: 'Ctrl+G', action: download, disabled: !jobDone },
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
          { label: 'Delete selected', shortcut: 'Del', action: removeSelected, disabled: !hasSel },
          { label: 'Delete all', shortcut: 'Ctrl+Del', action: deleteAll, disabled: !hasObjects },
          'sep',
          { label: 'Arrange', shortcut: 'A', action: arrangeAll, disabled: !hasObjects || project },
          'sep',
          { label: 'Preferences', shortcut: 'Ctrl+P' },
        ]}
      />
      <Menu
        title="View"
        items={[
          { label: 'Default View', shortcut: '0', action: () => view('0'), tip: 'Default View' },
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
        ]}
      />
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
  const busy = job.state === 'running' || job.state === 'submitting';
  const canExport = job.state === 'done' && !job.stale;
  const exportLabel = outputKind() === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code file';
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
        <button className="side-btn" disabled={!hasObjects || busy} onClick={() => void slice()}>
          Slice plate
        </button>
        <button className="side-btn" disabled={!canExport} onClick={download} title={job.stale ? 'Settings changed since the last slice; slice again' : ''}>
          {exportLabel}
        </button>
      </div>
    </div>
  );
}
