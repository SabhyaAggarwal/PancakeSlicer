import { preferenceText, savePreferences, savedPreferencesSnapshot, subscribeSavedPreferences, usePreference } from '../saved-preferences';
import { useEffect, useRef, useSyncExternalStore } from 'react';
import penUrl from '../../../docs/nonplanar/corpus/everyday/pen_holder.stl?url';
import knobUrl from '../../../tests/data/nonplanar/corpus/w_knob_knurled.stl?url';
import hookUrl from '../samples/shelf_hook.stl?url';
import { importFiles, openFileDialog, openProjectDialog } from '../actions';
import { getState, setState, subscribeStore, toast, useStore } from '../state/store';
import { PrinterPicker } from '../profiles/Pickers';
import { documentGeneration, onLeaveDocument } from '../state/history';
import { Icon } from './Icon';
import './first-run.css';
import { Modal, ModalTitle } from './Modal';

const SETUP_KEY = 'orca.setup';
const SAMPLES = [
  { url: penUrl, file: 'pen_holder.stl', label: 'Pen holder', tip: 'Load a simple everyday model' },
  { url: knobUrl, file: 'knob.stl', label: 'Knob', tip: 'An everyday part with a rounded, knurled top' },
  { url: hookUrl, file: 'shelf_hook.stl', label: 'Shelf hook', tip: 'A 90° ledge that prints without support' },
];

async function addSample(url: string, file: string) {
  setState({ loading: `Loading ${file}` });
  try {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
    await importFiles([new File([await r.blob()], file)]);
  } catch (e) {
    if (getState().loading === `Loading ${file}`) setState({ loading: null });
    toast(`Sample download failed: ${e instanceof Error ? e.message : e}`);
  }
}

function PrinterChoice() {
  return <div className="fr-printer"><label className="fr-label">Printer</label><PrinterPicker/></div>;
}

const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? '⌘' : 'Ctrl+';

function DropZone() {
  return (
    <div className="fr-drop">
      <Icon name="design_import_mesh" size={40} className="fr-drop-icon" />
      <div className="fr-drop-title">Drop a model</div>
      <div className="fr-drop-types">STL · OBJ · 3MF</div>
      <div className="fr-actions">
        <button type="button" className="btn-primary fr-add" onClick={openFileDialog} title={`Add model (${MOD}I)`}>
          Add model<kbd>{MOD}I</kbd>
        </button>
        <button type="button" className="btn-ghost" onClick={openProjectDialog} title={`Open project (${MOD}O)`}>Open project</button>
      </div>
      <div className="fr-samples">
        <span>or try</span>
        {SAMPLES.map((s) => (
          <button key={s.file} type="button" className="fr-sample" title={s.tip} onClick={() => void addSample(s.url, s.file)}>
            {s.label}
          </button>
        ))}
      </div>
    </div>
  );
}

let firstVisitObserver: (() => void) | null = null;
onLeaveDocument(() => firstVisitObserver?.());
function afterFirstVisit() {
  if(firstVisitObserver || getState().objects.length)return;
  const generation=documentGeneration();
  const root = document.documentElement;
  let completed=false;
  const stop=()=>{unsubscribe();firstVisitObserver=null;delete root.dataset.nudge;};
  const check = () => {
    const s = getState(),preferences=savedPreferencesSnapshot();
    if(documentGeneration()!==generation || !preferences.hydrated || preferences.state==='failed' || s.job.state!=='idle'
      || !completed && (preferenceText(SETUP_KEY) || preferenceText('orca.printer'))) { stop(); return; }
    if(s.objects.length && !completed){completed=true;savePreferences({[SETUP_KEY]:'1'});root.dataset.nudge='slice';}
    else if(completed && !s.objects.length)stop();
  };
  const unsubscribe=subscribeStore(check);
  firstVisitObserver=stop;
}

export default function EmptyPlate() {
  const preferences=useSyncExternalStore(subscribeSavedPreferences,savedPreferencesSnapshot);
  const setup=usePreference(SETUP_KEY),printer=usePreference('orca.printer');
  const firstVisit=preferences.hydrated && preferences.state!=='failed' && !setup && !printer;
  useEffect(()=>{if(firstVisit)afterFirstVisit();},[firstVisit]);
  return (
    <div className="empty-plate">
      {firstVisit && <PrinterChoice />}
      <DropZone />
    </div>
  );
}

export function SetupWizard({ close }: { close(): void }) {
  const count = useStore((s) => s.objects.length);
  const start = useRef(count);
  useEffect(() => { if (count > start.current) close(); }, [count]);
  return (
    <Modal label="Setup Wizard" onClose={close} className="small setup-wizard" returnFocus=".topbar .menu-title">
      <ModalTitle title="Setup Wizard" onClose={close} />
      <div className="setup-wizard-body">
        <PrinterChoice />
        <DropZone />
      </div>
      <div className="modal-buttons">
        <span style={{ flex: 1 }} />
        <button type="button" className="btn primary" data-autofocus onClick={close}>OK</button>
      </div>
    </Modal>
  );
}
