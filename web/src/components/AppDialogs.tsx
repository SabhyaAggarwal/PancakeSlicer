import { lazy, Suspense, useSyncExternalStore } from 'react';
import { setState, useStore } from '../state/store';
import { setThemePref, subscribeTheme, themePref, type ThemePref } from '../theme';
import { Modal, ModalTitle } from './Modal';

const About = lazy(() => import('./About'));
const SendDialog = lazy(() => import('./SendDialog'));
const SetupWizard = lazy(() => import('./FirstRun').then((m) => ({ default: m.SetupWizard })));

const close = () => setState({ appDialog: null });
const mod = /Mac|iPhone|iPad/.test(navigator.platform) ? 'Cmd' : 'Ctrl';

const THEMES: [ThemePref, string][] = [['system', 'System'], ['light', 'Light'], ['dark', 'Dark']];

function setDeveloperMode(developerMode: boolean) {
  try { localStorage.setItem('orca.developer_mode', developerMode ? '1' : '0'); } catch {  }
  setState({ developerMode });
}

function Preferences() {
  const pref = useSyncExternalStore(subscribeTheme, themePref);
  const developerMode = useStore((s) => s.developerMode);
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
          <legend>Develop mode</legend>
          <label className="prefs-row" title="Show the developer-only settings">
            <input type="checkbox" checked={developerMode} onChange={(e) => setDeveloperMode(e.target.checked)} />
            Develop mode
          </label>
        </fieldset>
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
    [`${mod}+P`, 'Preferences'], ['?', 'Show keyboard shortcuts list'],
    [`${mod}+0 … ${mod}+6 (or 0 … 6)`, 'Camera view: Default, Top, Bottom, Front, Rear, Left, Right'],
    ['Esc', 'Deselect All'],
  ]],
  ['Prepare', [
    ['Del / Backspace', 'Delete Selected'], [`${mod}+D / ${mod}+Del`, 'Delete All'], ['+ / -', 'Add / Remove instance'],
    ['V', 'Toggle printable for object/part'], ['A', 'Arrange all objects'], ['Q / Shift+Q', 'Auto orient selected / all objects'],
    ['Arrow keys', 'Move selection 10 mm (Shift: 1 mm)'], ['Page Up / Page Down', 'Rotate selection 45 degrees counterclockwise / clockwise'],
    ['M / R / S', 'Gizmo move / rotate / scale'], ['I / O', 'Zoom in / out'],
    ['Left mouse', 'Rotate View'], ['Right mouse', 'Pan View'], ['Mouse wheel', 'Zoom View'],
  ]],
  ['Preview', [
    ['Up / Down', 'Vertical slider - Move active thumb Up / Down'], ['Left / Right', 'Horizontal slider - Move active thumb Left / Right'],
    ['Home / End', 'Horizontal slider - Move to start / last position'], ['I / O', 'Zoom in / out'],
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

export default function AppDialog({ which }: { which: 'about' | 'preferences' | 'shortcuts' | 'send' | 'setup' }) {
  if (which === 'send') return <Suspense fallback={null}><SendDialog close={close} /></Suspense>;
  if (which === 'setup') return <Suspense fallback={null}><SetupWizard close={close} /></Suspense>;
  if (which === 'preferences') return <Preferences />;
  if (which === 'shortcuts') return <Shortcuts />;
  return <Suspense fallback={null}><About close={close} /></Suspense>;
}
