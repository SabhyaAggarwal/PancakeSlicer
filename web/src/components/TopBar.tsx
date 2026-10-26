import { lazy, Suspense, useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { arrangeAll, canExport, fileUse, STALE_RESULT_TIP, download, newProject, openFileDialog, openProjectDialog, outputKind, outsideVolumeWarning, saveProject, slice } from '../actions';
import { deleteAll, removeSelected, setState, useStore } from '../state/store';
import { canPaste, copySelection, pasteClipboard, selectAll } from '../edit';
import { historyVersion, redo, redoName, subscribeHistory, undo, undoName } from '../state/history';
import { CAMERA_VIEWS, VIEW_DIRECTIONS, cameraCommand, cameraSnapshot, subscribeCamera, perspective, autoPerspective } from '../camera';
import { Icon } from './Icon';
import { canAddPlate, duplicatePlate, exportAll, exportablePlates, sliceAll } from '../plater/plates';
import { openGcode } from '../preview/jump';
import { toggleViewOption, useViewOptions } from '../plater/viewOptions';
import { toggleNonplanarView, useNonplanarView } from '../patch-preview';
import { canCalibrate, canLoadStep } from '../api/client';

type MenuItem = { label: string; shortcut?: string; action?: () => void; disabled?: boolean; tip?: string; checked?: boolean; role?: 'menuitemradio' | 'menuitemcheckbox'; sub?: MenuItem[] } | 'sep';

const popItems = (pop: Element | null | undefined) =>
  pop ? [...pop.querySelectorAll<HTMLButtonElement>(':scope > .menu-item:not(:disabled), :scope > .menu-subwrap > .menu-item:not(:disabled)')] : [];

function Menu({ title, items }: { title: string; items: MenuItem[] }) {
  const [open, setOpen] = useState(false);
  const [sub, setSub] = useState<string | null>(null);
  const ref = useRef<HTMLDivElement>(null);
  const button = useRef<HTMLButtonElement>(null);
  const focusItem = useRef<'first' | 'last' | null>(null);
  useEffect(() => {
    if (!open) return setSub(null);
    const close = (e: PointerEvent) => !ref.current?.contains(e.target as Node) && setOpen(false);
    window.addEventListener('pointerdown', close);
    const enabled = popItems(ref.current!.querySelector('.menu-pop'));
    if (focusItem.current) (focusItem.current === 'first' ? enabled[0] : enabled[enabled.length - 1])?.focus();
    focusItem.current = null;
    return () => window.removeEventListener('pointerdown', close);
  }, [open]);
  const show = (where: 'first' | 'last' | null) => {
    focusItem.current = where;
    setOpen(true);
  };
  const onKey = (e: React.KeyboardEvent) => {
    const active = document.activeElement as HTMLButtonElement;
    const enabled = popItems(active.closest('.menu-pop') ?? ref.current!.querySelector('.menu-pop'));
    const at = enabled.indexOf(document.activeElement as HTMLButtonElement);
    const titles = [...document.querySelectorAll<HTMLButtonElement>('.topbar .menu-title')];
    const neighbour = (d: number) => titles[(titles.indexOf(button.current!) + d + titles.length) % titles.length];
    if (e.key === 'Escape' && open) {
      setOpen(false);
      button.current!.focus();
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      if (!open) show(e.key === 'ArrowDown' ? 'first' : 'last');
      else enabled[(at + (e.key === 'ArrowDown' ? 1 : -1) + enabled.length) % enabled.length]?.focus();
    } else if ((e.key === 'Home' || e.key === 'End') && open) enabled[e.key === 'Home' ? 0 : enabled.length - 1]?.focus();
    else if (e.key === 'ArrowRight' && open && active.dataset.sub) {
      setSub(active.dataset.sub);
      requestAnimationFrame(() => popItems(active.nextElementSibling)[0]?.focus());
    } else if (e.key === 'ArrowLeft' && open && active.closest('.menu-sub')) {
      setSub(null);
      (active.closest('.menu-subwrap')!.firstElementChild as HTMLButtonElement).focus();
    } else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && open) {
      setOpen(false);
      const next = neighbour(e.key === 'ArrowRight' ? 1 : -1);
      next.focus();
      next.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true }));
    } else if (e.key === 'Tab' && open) {
      setOpen(false);
      return;
    } else return;
    e.preventDefault();
    e.stopPropagation();
  };
  return (
    <div className="menu" ref={ref} onKeyDown={onKey}>
      <button ref={button} className={`menu-title ${open ? 'open' : ''}`} aria-haspopup="menu" aria-expanded={open}
        onClick={(e) => (open ? setOpen(false) : show(e.detail === 0 ? 'first' : null))}>
        {title}
      </button>
      {open && (
        <div className="menu-pop" role="menu" aria-label={title}>
          {items.map(function item(it: MenuItem, i: number): React.ReactNode {
            if (it === 'sep') return <div key={i} className="menu-sep" role="separator" />;
            if (it.sub)
              return (
                <div key={it.label} className="menu-subwrap" onPointerEnter={() => setSub(it.label)}>
                  <button className="menu-item" data-sub={it.label} aria-haspopup="menu" aria-expanded={sub === it.label} title={it.tip} onClick={() => setSub(it.label)}>
                    <span>{it.label}</span>
                    <span className="menu-key" aria-hidden="true">▸</span>
                  </button>
                  {sub === it.label && <div className="menu-pop menu-sub" role="menu" aria-label={it.label}>{it.sub.map(item)}</div>}
                </div>
              );
            return (
              <button
                key={it.label}
                className="menu-item"
                role={it.role ?? 'menuitem'}
                aria-checked={it.checked}
                disabled={it.disabled || !it.action}
                title={it.tip}
                onPointerEnter={(e) => !e.currentTarget.closest('.menu-sub') && setSub(null)}
                onClick={() => {
                  setOpen(false);
                  button.current!.focus();
                  it.action?.();
                }}
              >
                <span>{it.label}</span>
                {it.shortcut && <span className="menu-key" aria-hidden="true">{it.shortcut}</span>}
              </button>
            );
          })}
        </div>
      )}
    </div>
  );
}

const exportStl = (multi: boolean) => import('../exportStl').then((m) => m.exportStl(false, multi));

const ArchiveDialog = lazy(() => import('./ArchiveDialog'));

const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? 'Cmd' : 'Ctrl';

export function TopBar() {
  useSyncExternalStore(subscribeCamera, cameraSnapshot);
  const view = useViewOptions();
  const nonplanarView = useNonplanarView();
  const [archive, setArchive] = useState<File[] | null>(null);
  useSyncExternalStore(subscribeHistory, historyVersion);
  const undoLabel = undoName(), redoLabel = redoName();
  const exportEnabled = useStore(canExport);
  const plates = useStore((s) => s.projectPlates.length > 1);
  const plateTools = useStore(canAddPlate);
  const exportAllEnabled = useStore((s) => s.projectPlates.length > 1 && s.job.state !== 'running' && s.job.state !== 'submitting' && exportablePlates(s).length > 0);
  const stale = useStore((s) => s.job.stale);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const hasProjectObjects = useStore((s) => s.objects.length > 0 || s.projectPlates.some((p) => p.objects.length > 0));
  const hasSel = useStore((s) => !!s.selected);
  const hasProject = useStore((s) => !!s.project);
  const brimEarsKeys = useStore((s) => !!s.features.brim_ears);
  const hasMeshes = useStore((s) => s.objects.some((o) => !o.project?.placeholder));
  const pasteEnabled = useStore(canPaste);
  const mock = useStore((s) => s.api?.mock);
  const name = useStore((s) => (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf)$/i, ''));
  const kind = useStore(outputKind);
  const exportLabel = kind === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code';
  const exportTip = kind === 'gcode.3mf' ? 'Export current sliced file' : 'Export current plate as G-code';
  const hasPrinters = useStore((s) => s.printers.length > 0 || s.printerEdit);
  return (
    <div className="topbar">
      <Icon name="OrcaSlicer" size={20} className="topbar-logo" />
      {archive && <Suspense fallback={null}><ArchiveDialog files={archive} close={() => setArchive(null)} /></Suspense>}
      <Menu
        title="File"
        items={[
          { label: 'New Project', shortcut: `${MOD}+N`, action: newProject, tip: 'Start a new project' },
          { label: 'Save Project', shortcut: `${MOD}+S`, action: () => void saveProject(), disabled: !hasProjectObjects, tip: 'Save current project to file' },
          { label: 'Save Project as...', shortcut: `${MOD}+Shift+S`, action: () => void saveProject(true), disabled: !hasProjectObjects, tip: 'Save current project as' },
          { label: 'Open Project...', shortcut: `${MOD}+O`, action: openProjectDialog, tip: 'Open a project file' },
          { label: 'Recent projects...', action: () => setState({ sessionDialog: 'recent' }) },
          'sep',
          { label: canLoadStep() ? 'Import 3MF/STL/STEP/OBJ...' : 'Import 3MF/STL/OBJ...', shortcut: `${MOD}+I`, action: openFileDialog, tip: 'Load a model' },
          { label: 'Import ZIP Archive...', action: () => void import('./ArchiveDialog').then((m) => m.importZipArchive(setArchive)), tip: 'Load models contained within a ZIP archive' },
          { label: 'Open G-code...', action: () => openGcode(), tip: 'Open a G-code file' },
          { label: 'Import Configs...', action: () => setState({ presetUx: { kind: 'import' } }), tip: 'Load configs' },
          'sep',
          { label: exportLabel + '...', shortcut: `${MOD}+G`, action: download, disabled: !exportEnabled, tip: stale ? STALE_RESULT_TIP : exportTip },
          ...(plates ? [{ label: 'Export all plate sliced file...', action: () => void exportAll(), disabled: !exportAllEnabled, tip: 'Export all plate sliced file' }] : []),
          ...(hasPrinters ? [{ label: 'Send...', action: () => setState({ appDialog: 'send' }), disabled: !exportEnabled, tip: 'Upload the sliced file to a printer' }] : []),
          { label: 'Export all objects as one STL...', action: () => void exportStl(false), disabled: !hasMeshes, tip: 'Export all objects as one STL' },
          { label: 'Export all objects as STLs...', action: () => void exportStl(true), disabled: !hasMeshes, tip: 'Export all objects as STLs' },
          { label: 'Export toolpaths as OBJ...', tip: 'Export toolpaths as OBJ' },
          { label: 'Export Preset Bundle...', action: () => setState({ presetUx: { kind: 'export' } }), tip: 'Export current configuration to files' },
          'sep',
          { label: 'Quit', tip: 'Quit' },
        ]}
      />
      <Menu
        title="Edit"
        items={[
          { label: undoLabel ? `Undo ${undoLabel}` : 'Undo', shortcut: `${MOD}+Z`, action: undo, disabled: !undoLabel },
          { label: redoLabel ? `Redo ${redoLabel}` : 'Redo', shortcut: `${MOD}+Y`, action: redo, disabled: !redoLabel },
          'sep',
          { label: 'Copy', shortcut: `${MOD}+C`, action: copySelection, disabled: !hasSel, tip: 'Copy selection to clipboard' },
          { label: 'Paste', shortcut: `${MOD}+V`, action: pasteClipboard, disabled: !pasteEnabled, tip: 'Paste clipboard' },
          { label: 'Delete selected', shortcut: 'Del', action: removeSelected, disabled: !hasSel, tip: 'Deletes the current selection' },
          { label: 'Delete all', shortcut: `${MOD}+Del`, action: deleteAll, disabled: !hasObjects, tip: 'Deletes all objects' },
          { label: 'Clone Selected', shortcut: `${MOD}+K`, action: () => setState({ appDialog: 'clone' }), disabled: !hasSel || hasProject, tip: 'Clone copies of selections' },
          'sep',
          { label: 'Duplicate Current Plate', action: duplicatePlate, disabled: !plateTools, tip: 'Duplicate the current plate' },
          'sep',
          { label: 'Select All', shortcut: `${MOD}+A`, action: selectAll, disabled: !hasObjects, tip: 'Selects all objects' },
          { label: 'Deselect All', shortcut: 'Esc', action: () => setState({ selected: null }), disabled: !hasSel, tip: 'Deselects all objects' },
          'sep',
          { label: 'Arrange', shortcut: 'A', action: arrangeAll, disabled: !hasObjects },
          'sep',
          { label: 'Search settings...', shortcut: `${MOD}+F`, action: () => setState({ presetUx: { kind: 'search' } }), tip: 'Search in all printer, filament and process settings' },
          { label: 'Preferences', shortcut: `${MOD}+P`, action: () => setState({ appDialog: 'preferences' }) },
        ]}
      />
      <Menu
        title="View"
        items={[
          ...CAMERA_VIEWS.map((label, i) => ({ label, shortcut: String(i), action: () => cameraCommand(VIEW_DIRECTIONS[i]), tip: i ? `${label} View` : label })),
          'sep',
          { label: 'Use Perspective View', role: 'menuitemradio', checked: perspective, action: () => cameraCommand('perspective') },
          { label: 'Use Orthogonal View', role: 'menuitemradio', checked: !perspective, action: () => cameraCommand('orthogonal') },
          { label: 'Auto Perspective', role: 'menuitemcheckbox', checked: autoPerspective, action: () => cameraCommand('auto') },
          'sep',
          { label: 'Show G-code Window', shortcut: 'C', role: 'menuitemcheckbox', checked: view.gcodeWindow, action: () => toggleViewOption('gcodeWindow'), tip: 'Show G-code window in Preview scene.' },
          { label: 'Show 3D Navigator', role: 'menuitemcheckbox', checked: view.navigator, action: () => toggleViewOption('navigator'), tip: 'Show 3D navigator in Prepare and Preview scene.' },
          { label: 'Show Gridlines', role: 'menuitemcheckbox', checked: view.gridlines, action: () => toggleViewOption('gridlines'), tip: 'Show Gridlines on plate' },
          'sep',
          { label: 'Show Labels', shortcut: brimEarsKeys ? `${MOD}+E` : 'E', role: 'menuitemcheckbox', checked: view.labels, action: () => toggleViewOption('labels'), tip: 'Show object labels in 3D scene.' },
          { label: 'Show Overhang', role: 'menuitemcheckbox', checked: view.overhang, action: () => toggleViewOption('overhang'), tip: 'Show object overhang highlight in 3D scene.' },
          { label: 'Show Non-planar Surfaces', role: 'menuitemcheckbox', checked: nonplanarView, action: toggleNonplanarView, tip: "Color top surfaces by their slope against the print head's limit and show where curved top shells will be printed." },
          { label: 'Show Selected Outline (beta)', role: 'menuitemcheckbox', checked: view.outline, action: () => toggleViewOption('outline'), tip: 'Show outline around selected object in 3D scene.' },
          { label: 'Show X-Ray', role: 'menuitemcheckbox', checked: view.xray, action: () => toggleViewOption('xray'), tip: 'Draw models see-through, so hidden parts show.' },
          { label: 'Show Center of Mass', role: 'menuitemcheckbox', checked: view.com, action: () => toggleViewOption('com'), tip: 'Show the center of mass of each object in 3D scene.' },
          { label: 'Show Axes', role: 'menuitemcheckbox', checked: view.axes, action: () => toggleViewOption('axes'), tip: 'Show the X, Y and Z axes at the bed origin.' },
          { label: 'Show Wireframe', shortcut: `${MOD}+Shift+Enter`, role: 'menuitemcheckbox', checked: view.wireframe, action: () => toggleViewOption('wireframe'), tip: 'Show wireframes in 3D scene.' },
        ]}
      />
      {canCalibrate() && <Menu
        title="Calibration"
        items={[
          { label: 'Temperature', action: () => setState({ calibDialog: 'temperature' }), tip: 'Temperature Calibration' },
          { label: 'Max flowrate', action: () => setState({ calibDialog: 'max-flowrate' }), tip: 'Max flowrate' },
          { label: 'Pressure advance', action: () => setState({ calibDialog: 'pressure-advance' }), tip: 'Pressure advance' },
          { label: 'Flow ratio', action: () => setState({ calibDialog: 'flow-ratio' }), tip: 'Flow Rate Calibration' },
          { label: 'Retraction', action: () => setState({ calibDialog: 'retraction' }), tip: 'Retraction' },
          { label: 'Cornering', action: () => setState({ calibDialog: 'cornering' }), tip: 'Cornering calibration' },
          { label: 'Input Shaping', sub: [
            { label: 'Input Shaping Frequency', action: () => setState({ calibDialog: 'input-shaping-freq' }), tip: 'Input Shaping Frequency' },
            { label: 'Input Shaping Damping/zeta factor', action: () => setState({ calibDialog: 'input-shaping-damp' }), tip: 'Input Shaping Damping/zeta factor' },
          ] },
          { label: 'VFA', action: () => setState({ calibDialog: 'vfa' }), tip: 'VFA' },
          { label: 'Non-planar', action: () => setState({ calibDialog: 'nonplanar' }), tip: 'Non-planar calibration' },
          'sep',
          { label: 'Calibration Guide', action: () => window.open('https://www.orcaslicer.com/wiki/calibration_guide', '_blank', 'noopener'), tip: 'Calibration Guide' },
        ]}
      />}
      <Menu
        title="Help"
        items={[
          { label: 'What you can do', action: () => setState({ appDialog: 'what' }), tip: 'What the web app does, with screenshots' },
          { label: 'Keyboard Shortcuts', shortcut: '?', action: () => setState({ appDialog: 'shortcuts' }), tip: 'Show the list of keyboard shortcuts' },
          { label: 'Setup Wizard', action: () => setState({ appDialog: 'setup' }), tip: 'Setup Wizard' },
          'sep',
          { label: 'Show Configuration Folder', tip: 'Show Configuration Folder' },
          { label: 'Troubleshoot Center' },
          { label: 'Show Tip of the Day', tip: 'Show Tip of the Day' },
          { label: 'Check for Updates', tip: 'Check for Updates' },
          'sep',
          { label: 'About OrcaSlicer', action: () => setState({ appDialog: 'about' }), tip: 'Version, licence and Corresponding Source' },
        ]}
      />
      <button type="button" className="icon-btn topbar-btn" title={undoLabel ? `Undo ${undoLabel} (${MOD}+Z)` : 'Undo'} aria-label="Undo" disabled={!undoLabel} onClick={undo}>
        <Icon name={undoLabel ? 'topbar_undo' : 'topbar_undo_inactive'} size={20} />
      </button>
      <button type="button" className="icon-btn topbar-btn" title={redoLabel ? `Redo ${redoLabel} (${MOD}+Y)` : 'Redo'} aria-label="Redo" disabled={!redoLabel} onClick={redo}>
        <Icon name={redoLabel ? 'topbar_redo' : 'topbar_redo_inactive'} size={20} />
      </button>
      <div className="topbar-title">
        {name ?? 'Untitled'}
        {mock && <span className="mock-badge" title="The slicing service did not answer; jobs are simulated">mock</span>}
      </div>
    </div>
  );
}

function SideOptions({ options, value, onChange }: { options: string[]; value: string; onChange(v: string): void }) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLSpanElement>(null);
  useEffect(() => {
    if (!open) return;
    const close = (e: PointerEvent) => !ref.current?.contains(e.target as Node) && setOpen(false);
    window.addEventListener('pointerdown', close);
    return () => window.removeEventListener('pointerdown', close);
  }, [open]);
  return (
    <span ref={ref} className="side-options">
      <button className="side-btn side-drop" aria-haspopup="menu" aria-expanded={open} onClick={() => setOpen(!open)}><Icon name="sidebutton_dropdown" size={14} /></button>
      {open && <div role="menu" className="menu-pop menu-pop-right">
        {options.map((o) => <button key={o} role="menuitemradio" aria-checked={o === value} className="menu-item" onClick={() => { onChange(o); setOpen(false); }}><span>{o}</span><span /></button>)}
      </div>}
    </span>
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
  const hasPrinters = useStore((s) => s.printers.length > 0 || s.printerEdit);
  const plates = useStore((s) => s.projectPlates.length > 1);
  const [sliceMode, setSliceMode] = useState('Slice plate');
  const [exportMode, setExportMode] = useState('plate');
  const allSlice = plates && sliceMode === 'Slice all';
  const allExport = plates && exportMode === 'all';
  const exportAllEnabled = useStore((s) => s.projectPlates.length > 1 && !busy && exportablePlates(s).length > 0);
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
        {allSlice ? (
          <button className="side-btn side-split" disabled={busy} title="Slice all plates" onClick={() => void sliceAll()}>Slice all</button>
        ) : (
          <button className={`side-btn ${plates ? 'side-split' : ''}`} disabled={!hasObjects || busy || !!outside} title={outside ?? (hasObjects ? 'Slice the plate into a printable file and show it in Preview' : 'Add a model to the plate first')} onClick={() => void slice()}>
            Slice plate
          </button>
        )}
        {plates && <SideOptions options={['Slice all', 'Slice plate']} value={sliceMode} onChange={setSliceMode} />}
        {allExport ? (
          <button className="side-btn side-split" disabled={!exportAllEnabled} onClick={() => void exportAll()} title={exportAllEnabled ? 'Export all sliced plates' : 'Slice the plates first'}>Export all sliced file</button>
        ) : (
          <button className={`side-btn ${plates ? 'side-split' : ''}`} disabled={!exportEnabled} onClick={download} title={job.stale ? STALE_RESULT_TIP : outside ?? (exportEnabled ? fileUse() : 'Slice the plate first')}>
            {exportLabel}
          </button>
        )}
        {plates && <SideOptions options={[exportLabel, 'Export all sliced file']} value={allExport ? 'Export all sliced file' : exportLabel}
          onChange={(v) => setExportMode(v === 'Export all sliced file' ? 'all' : 'plate')} />}
        {hasPrinters && (
          <button className="side-btn" disabled={!exportEnabled} onClick={() => setState({ appDialog: 'send' })} title={exportEnabled ? 'Upload the sliced file to a printer' : 'Slice the plate first'}>
            Send
          </button>
        )}
      </div>
    </div>
  );
}
