import { Dropdown } from './Dropdown';
import { lazy, Suspense, useState, useSyncExternalStore } from 'react';
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
            <Dropdown className="combo" value={camera.style} onValueChange={(next) => setCameraPrefs({ style: next as CameraPrefs['style'] })} options={[{ value: "default", label: 'Default' }, { value: "touchpad", label: 'Touchpad' }]} />
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
    [`${mod}+N`, 'New project'], [`${mod}+O`, 'Open project'], [`${mod}+S`, 'Save'], [`${mod}+Shift+S`, 'Save as'],
    [`${mod}+I`, 'Import models'], [`${mod}+R`, 'Slice'], [`${mod}+G`, 'Export'],
    [`${mod}+Z`, 'Undo'], [`${mod}+Y / ${mod}+Shift+Z`, 'Redo'], [`${mod}+F`, 'Search settings'], [`${mod}+P`, 'Preferences'],
    [`${mod}+\\`, 'Hide / show sidebar'], ['Tab', 'Prepare / Preview (in a 3D view)'], ['Shift+Tab / Esc', 'Leave the 3D view'], ['?', 'Shortcuts'],
    ['0 … 6', 'Views: default, top, bottom, front, rear, left, right'], ['I / O', 'Zoom in / out'],
  ]],
  ['Prepare', [
    [`${mod}+A`, 'Select all'], ['Esc', 'Deselect'], [`${mod}+C / ${mod}+X / ${mod}+V`, 'Copy / cut / paste'], [`${mod}+K`, 'Clone'],
    ['Del / Backspace', 'Delete'], [`${mod}+D / ${mod}+Del`, 'Delete all'], ['+ / -', 'Add / remove instance'],
    ['V', 'Printable on / off'], ['F2', 'Rename'], ['A / Shift+A', 'Arrange all / this plate'], ['Q / Shift+Q', 'Orient selected / all'],
    ['Arrows', 'Move 10 mm (Shift: 1 mm)'], ['PgUp / PgDn', 'Rotate 45°'],
    ['M / R / S', 'Move / rotate / scale'], ['F', 'Lay on face'], ['C', 'Cut'], ['U', 'Measure'],
    ['B', 'Boolean'], ['T', 'Text'], ['E', 'Brim ears'], ['L / P / H / N', 'Paint support / seam / fuzzy skin / color'],
    [`${mod}+E`, 'Labels'], [`${mod}+Shift+Enter`, 'Wireframe'],
    ['Left drag', 'Orbit'], ['Right drag', 'Pan'], ['Wheel', 'Zoom'],
  ]],
  ['Painting', [
    ['C / S', 'Circle / sphere'], ['F / G', 'Fill / gap fill'], ['T / H', 'Triangle / height range'], ['1 … 9', 'Filament'],
  ]],
  ['Preview', [
    ['Up / Down', 'Layer up / down'], ['Left / Right', 'Move back / forward'], ['Home / End', 'First / last move'],
    ['L', 'One layer'], ['Shift+G', 'Go to layer'], ['C', 'G-code window'],
  ]],
];

function Keys({ keys }: { keys: string }) {
  return (
    <span className="keys">
      {keys.split(' / ').map((alt, i) => (
        <span key={i} className="keys-alt">
          {i > 0 && <span className="keys-or">/</span>}
          {(alt === '+' ? ['+'] : alt.split(/(?<=.)\+(?=.)/)).map((k) => <kbd key={k}>{k}</kbd>)}
        </span>
      ))}
    </span>
  );
}

function Shortcuts() {
  const [query, setQuery] = useState('');
  const q = query.trim().toLowerCase();
  const tab = useStore((s) => s.tab);
  const pages = [...SHORTCUTS].sort(([a], [b]) => Number(b.toLowerCase() === tab) - Number(a.toLowerCase() === tab))
    .map(([page, rows]) => [page, rows.filter(([k, what]) => !q || what.toLowerCase().includes(q) || k.toLowerCase().includes(q))] as const)
    .filter(([, rows]) => rows.length);
  return (
    <Modal className="shortcuts-dialog" label="Keyboard Shortcuts" onClose={close} backdropClose>
      <ModalTitle title="Keyboard Shortcuts" onClose={close} />
      <div className="shortcuts-search">
        <input type="text" placeholder="Filter" aria-label="Filter shortcuts" value={query} data-autofocus onChange={(e) => setQuery(e.target.value)} />
      </div>
      <div className="shortcuts-body" tabIndex={0}>
        {pages.map(([page, rows]) => (
          <section key={page}>
            <h3>{page}</h3>
            <table className="shortcuts-table">
              <tbody>
                {rows.map(([key, what]) => (
                  <tr key={key + what}><th scope="row"><Keys keys={key} /></th><td>{what}</td></tr>
                ))}
              </tbody>
            </table>
          </section>
        ))}
        {!pages.length && <div className="shortcuts-none">No match</div>}
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
