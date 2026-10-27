import { lazy, Suspense, useSyncExternalStore } from 'react';
import { markStale, setState, useStore } from '../state/store';
import { setThemePref, subscribeTheme, themePref, type ThemePref } from '../theme';
import { cameraPrefsSnapshot, setCameraPrefs, subscribeCamera, type CameraPrefs } from '../camera';
import { Modal, ModalTitle } from './Modal';

const About = lazy(() => import('./About'));
const PlateSettings = lazy(() => import('./PlateSettings'));
const CloneDialog = lazy(() => import('./CloneDialog'));
const SimplifyDialog = lazy(() => import('./SimplifyDialog'));
const FilamentsDialog = lazy(() => import('./FilamentsDialog'));
const ObjectTable = lazy(() => import('./ObjectTable'));
const CreatePrinter = lazy(() => import('./CreatePrinter'));
const SendDialog = lazy(() => import('./SendDialog'));
const WhatYouCanDo = lazy(() => import('./WhatYouCanDo'));
const SetupWizard = lazy(() => import('./FirstRun').then((m) => ({ default: m.SetupWizard })));

const close = () => setState({ appDialog: null });
const mod = /Mac|iPhone|iPad/.test(navigator.platform) ? 'Cmd' : 'Ctrl';

const THEMES: [ThemePref, string][] = [['system', 'System'], ['light', 'Light'], ['dark', 'Dark']];

function setDeveloperMode(developerMode: boolean) {
  try { localStorage.setItem('orca.developer_mode', developerMode ? '1' : '0'); } catch {  }
  setState({ developerMode });
}

function setMixedTemperatures(on: boolean) {
  try { localStorage.setItem('orca.enable_high_low_temp_mixed_printing', on ? '1' : '0'); } catch {  }
  setState((s) => ({ mixedTemperatures: on, job: markStale(s.job) }));
}

function Preferences() {
  const pref = useSyncExternalStore(subscribeTheme, themePref);
  const camera = useSyncExternalStore(subscribeCamera, cameraPrefsSnapshot);
  const developerMode = useStore((s) => s.developerMode);
  const mixed = useStore((s) => s.mixedTemperatures);
  const canMix = useStore((s) => !!s.features.mixed_temperatures);
  return (
    <Modal className="small prefs-dialog" label="Preferences" onClose={close}>
      <ModalTitle title="Preferences" onClose={close} />
      <div className="prefs-body">
        <fieldset className="prefs-group">
          <legend>Dark Mode</legend>
          {THEMES.map(([value, label]) => (
            <label key={value} className="prefs-row">
              <input type="radio" name="theme" value={value} checked={pref === value} data-autofocus={pref === value ? '' : undefined}
                onChange={() => setThemePref(value)} />
              {label}
            </label>
          ))}
        </fieldset>
        <fieldset className="prefs-group">
          <legend>Camera</legend>
          <label className="prefs-row" title={'Select camera navigation style.\nDefault: LMB+move for rotation, RMB/MMB+move for panning.\nTouchpad: Alt+move for rotation, Shift+move for panning.'}>
            Camera style
            <select className="combo" value={camera.style} onChange={(e) => setCameraPrefs({ style: e.target.value as CameraPrefs['style'] })}>
              <option value="default">Default</option>
              <option value="touchpad">Touchpad</option>
            </select>
          </label>
          <label className="prefs-row" title="Multiplies the orbit speed for finer or coarser camera movement.">
            Orbit speed multiplier
            <input className="text num" type="number" min={0.05} max={2} step={0.05} value={camera.orbitMult}
              onChange={(e) => { const v = Number(e.target.value); if (v >= 0.05 && v <= 2) setCameraPrefs({ orbitMult: v }); }} />
          </label>
          <label className="prefs-row" title="Zoom in towards the mouse pointer's position in the 3D view, rather than the 2D window center.">
            <input type="checkbox" checked={camera.zoomToMouse} onChange={(e) => setCameraPrefs({ zoomToMouse: e.target.checked })} />
            Zoom to mouse position
          </label>
          <label className="prefs-row" title="If enabled, reverses the direction of zoom with mouse wheel.">
            <input type="checkbox" checked={camera.reverseZoom} onChange={(e) => setCameraPrefs({ reverseZoom: e.target.checked })} />
            Reverse mouse zoom
          </label>
        </fieldset>
        <fieldset className="prefs-group">
          <legend>Develop mode</legend>
          <label className="prefs-row" title="Show the developer-only settings">
            <input type="checkbox" checked={developerMode} onChange={(e) => setDeveloperMode(e.target.checked)} />
            Develop mode
          </label>
        </fieldset>
        {canMix && (
          <fieldset className="prefs-group">
            <legend>Slicing</legend>
            <label className="prefs-row" title="With this option enabled, you can print materials with a large temperature difference together.">
              <input type="checkbox" checked={mixed} onChange={(e) => setMixedTemperatures(e.target.checked)} />
              Remove mixed temperature restriction
            </label>
          </fieldset>
        )}
      </div>
      <div className="modal-buttons center">
        <button type="button" className="btn primary" onClick={close}>OK</button>
      </div>
    </Modal>
  );
}

const SHORTCUTS: [string, [string, string][]][] = [
  ['Global', [
    [`${mod}+N`, 'New Project'], [`${mod}+O`, 'Open Project'], [`${mod}+S`, 'Save Project'], [`${mod}+Shift+S`, 'Save Project as'],
    [`${mod}+I`, 'Import geometry data from STL/STEP/3MF/OBJ/AMF files'], [`${mod}+R`, 'Slice plate'], [`${mod}+G`, 'Export plate sliced file'],
    [`${mod}+Z`, 'Undo'], [`${mod}+Y / ${mod}+Shift+Z`, 'Redo'], [`${mod}+F`, 'Search'], [`${mod}+P`, 'Preferences'], ['?', 'Show keyboard shortcuts list'],
    ['0 … 6', 'Camera view: Default, Top, Bottom, Front, Rear, Left, Right'], ['I / O', 'Zoom in / out'],
    ['Esc', 'Deselect All'], ['Tab', 'Switch between Prepare/Preview'],
  ]],
  ['Prepare', [
    [`${mod}+A`, 'Select all objects'], [`${mod}+C / ${mod}+X / ${mod}+V`, 'Copy / Cut / Paste to clipboard'], [`${mod}+K`, 'Clone selected'],
    ['Del / Backspace', 'Delete Selected'], [`${mod}+D / ${mod}+Del`, 'Delete All'], ['+ / -', 'Add / Remove instance'],
    ['V', 'Toggle printable for object/part'], ['F2', 'Rename'], ['A / Shift+A', 'Arrange all objects / the selected plate'], ['Q / Shift+Q', 'Auto orient selected / all objects'],
    ['Arrow keys', 'Move selection 10 mm (Shift: 1 mm)'], ['Page Up / Page Down', 'Rotate selection 45 degrees counterclockwise / clockwise'],
    ['M / R / S', 'Gizmo move / rotate / scale'], ['F', 'Gizmo place face on bed'], ['C', 'Gizmo cut'], ['U', 'Gizmo measure'],
    ['B', 'Gizmo mesh boolean'], ['T', 'Gizmo text emboss'], ['E', 'Gizmo brim ears'],
    ['L / P / H / N', 'Gizmo support / seam / fuzzy skin / color painting'],
    [`${mod}+E`, 'Show/Hide object labels'], [`${mod}+Shift+Enter`, 'Show/Hide wireframe'],
    ['Left mouse', 'Rotate View'], ['Right mouse', 'Pan View'], ['Mouse wheel', 'Zoom View'],
  ]],
  ['Painting', [
    ['C / S', 'Circle / Sphere'], ['F / G', 'Fill / Gap Fill'], ['T / H', 'Triangle / Height Range (color painting)'],
    ['1 … 9', 'Color painting: choose the filament'],
  ]],
  ['Preview', [
    ['Up / Down', 'Vertical slider - Move active thumb Up / Down'], ['Left / Right', 'Horizontal slider - Move active thumb Left / Right'],
    ['Home / End', 'Horizontal slider - Move to start / last position'], ['L', 'One layer mode'], ['Shift+G', 'Jump to layer'],
    ['C', 'Show/Hide G-code window'], ['I / O', 'Zoom in / out'],
  ]],
];

function Shortcuts() {
  return (
    <Modal className="shortcuts-dialog" label="Keyboard Shortcuts" onClose={close} backdropClose>
      <ModalTitle title="Keyboard Shortcuts" onClose={close} />
      <div className="shortcuts-body" tabIndex={0}>
        {SHORTCUTS.map(([page, rows]) => (
          <section key={page}>
            <h3>{page}</h3>
            <table className="shortcuts-table">
              <tbody>
                {rows.map(([key, what]) => (
                  <tr key={key + what}><th scope="row"><kbd>{key}</kbd></th><td>{what}</td></tr>
                ))}
              </tbody>
            </table>
          </section>
        ))}
      </div>
      <div className="modal-buttons center">
        <button type="button" className="btn primary" data-autofocus onClick={close}>OK</button>
      </div>
    </Modal>
  );
}

export default function AppDialog({ which }: { which: 'about' | 'preferences' | 'shortcuts' | 'send' | 'setup' | 'what' | 'plate' | 'clone' | 'simplify' | 'filaments' | 'objectTable' | 'createPrinter' }) {
  if (which === 'createPrinter') return <Suspense fallback={null}><CreatePrinter close={close} /></Suspense>;
  if (which === 'objectTable') return <Suspense fallback={null}><ObjectTable close={close} /></Suspense>;
  if (which === 'filaments') return <Suspense fallback={null}><FilamentsDialog close={close} /></Suspense>;
  if (which === 'simplify') return <Suspense fallback={null}><SimplifyDialog close={close} /></Suspense>;
  if (which === 'clone') return <Suspense fallback={null}><CloneDialog close={close} /></Suspense>;
  if (which === 'plate') return <Suspense fallback={null}><PlateSettings close={close} /></Suspense>;
  if (which === 'what') return <Suspense fallback={null}><WhatYouCanDo close={close} /></Suspense>;
  if (which === 'send') return <Suspense fallback={null}><SendDialog close={close} /></Suspense>;
  if (which === 'setup') return <Suspense fallback={null}><SetupWizard close={close} /></Suspense>;
  if (which === 'preferences') return <Preferences />;
  if (which === 'shortcuts') return <Shortcuts />;
  return <Suspense fallback={null}><About close={close} /></Suspense>;
}
