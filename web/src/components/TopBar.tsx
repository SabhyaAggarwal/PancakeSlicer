import { ProjectTitle } from '../projects/ProjectTitle';
import { MenuButton, type MenuEntry } from './Menu';
import { showProjects } from '../flow/project';
import { openPalette } from '../commands/registry';
import { lazy, Suspense, useState, useSyncExternalStore, type CSSProperties } from 'react';
import { arrangeAll, canExport, STALE_RESULT_TIP, download, newProject, openFileDialog, openProjectDialog, outputKind, outsideVolumeWarning, saveProject, slice } from '../actions';
import { deleteAll, removeSelected, setState, useStore } from '../state/store';
import { canPaste, copySelection, cutSelection, pasteClipboard, selectAll } from '../edit';
import { historyVersion, redo, redoName, redoNames, redoSteps, subscribeHistory, undo, undoName, undoNames, undoSteps } from '../state/history';
import { CAMERA_VIEWS, VIEW_DIRECTIONS, cameraCommand, cameraSnapshot, subscribeCamera, perspective, autoPerspective } from '../camera';
import { startOver, useDevReset } from '../dev-reset';
import { promptDialog } from './Ask';
import { openSetup } from '../onboarding/setup-flow';
import { renameProject } from '../projects/api';
import { useProject } from '../flow/project';
import { toggleHistory } from '../projects/versions';
import { Icon } from './Icon';
import { Caret } from './Caret';
import { canAddPlate, duplicatePlate, exportAll, exportablePlates, sliceAll } from '../plater/plates';
import { openGcode } from '../preview/jump';
import { subscribeVisibleToolpaths, visibleToolpaths } from '../preview/visible';
import { toggleViewOption, useViewOptions } from '../plater/viewOptions';
import { toggleNonplanarView, useNonplanarView } from '../patch-preview';
import { canCalibrate, canLoadStep } from '../api/client';
import { toggleSidebar, useSidebarHidden } from '../sidebar';
import { printerReady, showSettings } from '../workspace-sheet';
import { useTier } from '../breakpoints';
import { Glyph } from './Glyph';

type MenuItem = MenuEntry;
function Menu({title, items}: {title: string; items: MenuItem[]}) { return <MenuButton label={title} items={items} className="menu-title" />; }

const exportStl = (multi: boolean) => import('../exportStl').then((m) => m.exportStl(false, multi));

const ArchiveDialog = lazy(() => import('./ArchiveDialog'));

const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? 'Cmd' : 'Ctrl';

export function TopBar() {
  useSyncExternalStore(subscribeCamera, cameraSnapshot);
  const view = useViewOptions();
  const nonplanarView = useNonplanarView();
  const sidebarHidden = useSidebarHidden();
  const [archive, setArchive] = useState<File[] | null>(null);
  useSyncExternalStore(subscribeHistory, historyVersion);
  const undoLabel = undoName(), redoLabel = redoName();
  const exportDrc = useStore((st) => !!st.features.export_drc);
  const generic3mf = useStore((st) => !!st.features.export_generic_3mf);
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
  const name = useStore((s) => (s.project?.name ?? s.objects[0]?.name)?.replace(/\.(stl|obj|3mf|drc)$/i, ''));
  const toolpaths = useSyncExternalStore(subscribeVisibleToolpaths, () => !!visibleToolpaths());
  const tab = useStore((st) => st.tab);
  const exportName = name;
  const kind = useStore(outputKind);
  const exportLabel = kind === 'gcode.3mf' ? 'Export plate sliced file' : 'Export G-code';
  const exportTip = kind === 'gcode.3mf' ? 'Export current sliced file' : 'Export current plate as G-code';
  const hasPrinters = useStore((s) => s.printerConnections.enabled);
  const fileItems: MenuItem[] = [
          { label: 'New Project', shortcut: `${MOD}+N`, action: newProject, tip: 'Start a new project' },
          { label: 'Save Project', shortcut: `${MOD}+S`, action: () => void saveProject(), disabled: !hasProjectObjects, tip: 'Save current project to file' },
          { label: 'Save Project as...', shortcut: `${MOD}+Shift+S`, action: () => void saveProject(true), disabled: !hasProjectObjects, tip: 'Save current project as' },
          { label: 'Open Project...', shortcut: `${MOD}+O`, action: openProjectDialog, tip: 'Open a project file' },
          { label: 'Recent projects...', action: showProjects },
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
          ...(exportDrc ? [{ label: 'Export all objects as one DRC...', action: () => void import('../plater/exportModel').then((m) => m.exportModel('drc')), disabled: !hasMeshes, tip: 'Export all objects as one DRC' }] : []),
          ...(generic3mf ? [{ label: 'Export Generic 3MF...', action: () => void import('../plater/exportModel').then((m) => m.exportModel('generic3mf')), disabled: !hasMeshes, tip: 'Export 3MF file without using some 3mf-extensions' }] : []),
          { label: 'Export toolpaths as OBJ...', disabled: !toolpaths || tab !== 'preview', tip: 'Export toolpaths as OBJ',
            action: () => void import('../preview/objExport').then((m) => m.exportToolpathsObj(exportName || 'toolpaths')) },
          { label: 'Export Preset Bundle...', action: () => setState({ presetUx: { kind: 'export' } }), tip: 'Export current configuration to files' },
        ];
  const devReset = useDevReset();
  const projectId = useProject((p) => p.context.id), projectState = useProject((p) => p.value);
  const renameHere = async () => {
    const next = await promptDialog('Rename project', name ?? 'Untitled');
    if (!next || next === name) return;
    if (projectId && projectState !== 'blank') void renameProject(projectId, next); else setState({ projectName: next });
  };
  const appItems: MenuItem[] = [{label:'Add model…',action:openFileDialog},{label:'File',sub:fileItems},{label:'Settings',action:showSettings},{label:'Projects',action:showProjects},{label:'Rename project…',action:()=>void renameHere()},{label:'Setup',action:openSetup},{label:'Profiles',action:()=>setState({presetUx:{kind:'presets',type:'machine'}})},'sep',...(devReset ? [{label:'Start over',action:()=>void startOver().catch(()=>{})} as MenuItem,'sep' as const] : []),{label: 'Edit', sub: [
          { label: undoLabel ? `Undo ${undoLabel}` : 'Undo', shortcut: `${MOD}+Z`, action: undo, disabled: !undoLabel },
          { label: redoLabel ? `Redo ${redoLabel}` : 'Redo', shortcut: `${MOD}+Y`, action: redo, disabled: !redoLabel },
          ...(undoLabel || redoLabel ? [{ label: 'History', sub: [
            ...redoNames().reverse().map((name, i, all): MenuItem => ({ label: `Redo ${name}`, action: () => redoSteps(all.length - i) })),
            ...(undoLabel && redoLabel ? ['sep' as const] : []),
            ...undoNames().map((name, i): MenuItem => ({ label: `Undo ${name}`, action: () => undoSteps(i + 1) })),
          ] } satisfies MenuItem] : []),
          'sep',
          { label: 'Cut', shortcut: `${MOD}+X`, action: cutSelection, disabled: !hasSel, tip: 'Cut selection to clipboard' },
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
          { label: 'Search settings...', shortcut: `${MOD}+F`, action: () => openPalette('/'), tip: 'Search in all printer, filament and process settings' },
          { label: 'Preferences', shortcut: `${MOD}+P`, action: () => setState({ appDialog: 'preferences' }) },
        ]}, {label: 'View', sub: [
          ...CAMERA_VIEWS.map((label, i) => ({ label, shortcut: String(i), action: () => cameraCommand(VIEW_DIRECTIONS[i]), tip: i ? `${label} View` : label })),
          'sep',
          { label: 'Show Sidebar', shortcut: `${MOD}+\\`, role: 'menuitemcheckbox', checked: !sidebarHidden, action: () => toggleSidebar() },
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
        ]}, ...(canCalibrate() ? [{label: 'Calibration', sub: [
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
        ]} as MenuItem] : []), {label: 'Help', sub: [
          { label: 'What you can do', action: () => setState({ appDialog: 'shortcuts' }), tip: 'What the web app does, with screenshots' },
          { label: 'Keyboard Shortcuts', shortcut: '?', action: () => setState({ appDialog: 'shortcuts' }), tip: 'Show the list of keyboard shortcuts' },
          { label: 'Setup', action: openSetup, tip: 'Printer, profiles, connection and filaments' },
          'sep',
          { label: 'About PancakeSlicer', action: () => setState({ appDialog: 'about' }), tip: 'Version, licence and Corresponding Source' },
        ]}];
  return <header className="topbar compact-topbar">
    <div className="topbar-project-context"><MenuButton className="app-menu-button" label="Application menu" items={appItems}><img src="/logo-48.png" width={20} height={20} alt="" draggable={false}/><Caret className="app-menu-caret"/></MenuButton>
    <button type="button" className="menu-title projects-button" onClick={showProjects}>Projects</button>
    <Menu title="File" items={fileItems}/>
    </div>
    {mock && <span className="mock-badge">Demo</span>}
    {tab !== 'projects' && <TabBar/>}
    {archive && <Suspense fallback={null}><ArchiveDialog files={archive} close={() => setArchive(null)} /></Suspense>}
  </header>;
}

function SideOptions({ options, value, onChange }: { options: string[]; value: string; onChange(v: string): void }) {
  return <span className="side-options"><MenuButton className="btn slant side-drop" label="Output options" items={options.map(o => ({label: o, checked: o === value, action: () => onChange(o)}))}><Caret/></MenuButton></span>;
}

export function TabBar() {
  const tab = useStore((s) => s.tab);
  const job = useStore((s) => s.job);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const outside = useStore(outsideVolumeWarning);
  const busy = job.state === 'running' || job.state === 'submitting';
  const exportEnabled = useStore(canExport);
  const exportLabel = useStore(outputKind) === 'gcode.3mf' ? 'Export sliced file' : 'Export G-code';
  const printer = useStore(printerReady);
  const hasPrinters = useStore((s) => s.printerConnections.enabled);
  const plates = useStore((s) => s.projectPlates.length > 1);
  const saved = useProject((p) => p.value === 'editing' || p.value === 'saved' || p.value === 'saving');
  const [sliceMode, setSliceMode] = useState('Slice plate');
  const [exportMode, setExportMode] = useState('plate');
  const exportAllEnabled = useStore((s) => s.projectPlates.length > 1 && !busy && exportablePlates(s).length > 0);
  const tier = useTier(), wide = tier === 'wide', coarse = window.matchMedia('(pointer: coarse)').matches, folded = tier === 'narrow' || tier === 'phone' || (tier === 'medium' && coarse);
  const ready = job.state === 'done' && !job.stale && exportEnabled && !folded;
  const sliceClass = `${ready ? 'btn' : 'btn-primary'} slant${busy ? ' slicing' : ''}`, exportClass = `${ready ? 'btn-primary' : 'btn'} slant`;
  const progress = busy ? { '--p': `${Math.round(job.pct)}%` } as CSSProperties : undefined;
  const allSlice = wide && plates && sliceMode === 'Slice all';
  const allExport = wide && plates && exportMode === 'all';
  const more: MenuEntry[] = [
    ...(folded ? [{label: exportLabel, action: download, disabled: !exportEnabled, tip: job.stale ? STALE_RESULT_TIP : 'Slice the plate first'}] : []),
    ...(!wide && hasPrinters ? [{label: 'Send to printer', action: () => setState({appDialog: 'send'}), disabled: !exportEnabled}] : []),
    ...(tier === 'phone' && saved ? [{label: 'Version history', action: toggleHistory} satisfies MenuEntry] : []),
    ...(!wide && plates ? [{label: 'Slice all plates', action: () => void sliceAll(), disabled: !printer || busy}, {label: 'Export all plates', action: () => void exportAll(), disabled: !exportAllEnabled}] : []),
  ];
  return (
    <div className="tabbar">
      <div className="seg slant-seg view-switch" role="group" aria-label="View">
        <button type="button" aria-label="Prepare" aria-pressed={tab === 'prepare'} className="view-tab" onClick={() => setState({ tab: 'prepare' })}>
          <Icon name="tab_3d_active" size={16} />
          <span>Prepare</span>
        </button>
        <button type="button" aria-label="Preview" aria-pressed={tab === 'preview'} className="view-tab" onClick={() => setState({ tab: 'preview' })}>
          <Icon name="tab_preview_active" size={16} />
          <span>Preview</span>
        </button>
      </div>
      <div className="title-zone"><ProjectTitle/></div>
      <div className="tabbar-actions">
        {allSlice ? (
          <button className={sliceClass} style={progress} disabled={!printer || busy} onClick={() => void sliceAll()}>Slice all</button>
        ) : (
          <button className={sliceClass} style={progress} disabled={!hasObjects || !printer || busy || !!outside} onClick={() => void slice()}>
            {tier === 'phone' ? 'Slice' : 'Slice plate'}
          </button>
        )}
        {wide && plates && <SideOptions options={['Slice all', 'Slice plate']} value={sliceMode} onChange={setSliceMode} />}
        {!folded && <span className="export-actions">{allExport ? (
          <button className={exportClass} disabled={!exportAllEnabled} onClick={() => void exportAll()}>Export all sliced file</button>
        ) : (
          <button className={exportClass} disabled={!exportEnabled} onClick={download}>
            {exportLabel}
          </button>
        )}
        {wide && plates && <SideOptions options={[exportLabel, 'Export all sliced file']} value={allExport ? 'Export all sliced file' : exportLabel}
          onChange={(v) => setExportMode(v === 'Export all sliced file' ? 'all' : 'plate')} />}
        {wide && hasPrinters && (
          <button className="btn slant" disabled={!exportEnabled} onClick={() => setState({ appDialog: 'send' })}>
            Send
          </button>
        )}
        </span>}
        {more.length > 0 && <MenuButton className="btn slant output-overflow" label="More output actions" items={more}><Glyph name="more"/></MenuButton>}
      </div>
    </div>
  );
}
