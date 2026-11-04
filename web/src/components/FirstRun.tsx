import { preferenceText, savePreferences, savedPreferencesSnapshot, subscribeSavedPreferences, usePreference } from '../saved-preferences';
import { useEffect, useRef, useSyncExternalStore } from 'react';
import knobUrl from '../../../tests/data/nonplanar/corpus/w_knob_knurled.stl?url';
import hookUrl from '../samples/shelf_hook.stl?url';
import { openFileDialog, openProjectDialog } from '../actions';
import { handyUrl, importFetched } from '../plater/handy';
import { getState, subscribeStore, useStore } from '../state/store';
import { FilamentPicker, PrinterPicker } from '../profiles/Pickers';
import { startOver, useDevReset } from '../dev-reset';
import { printerReady } from '../workspace-sheet';
import { useState } from 'react';
import { documentGeneration, onLeaveDocument } from '../state/history';
import './first-run.css';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';

const SETUP_KEY = 'orca.setup';
const SAMPLES = [
  { url: handyUrl('3DBenchy.drc'), file: '3DBenchy.drc', label: 'Benchy' },
  { url: knobUrl, file: 'knob.stl', label: 'Knob' },
  { url: hookUrl, file: 'shelf_hook.stl', label: 'Shelf hook' },
];

function PrinterChoice() {
  return <div className="fr-printer"><label className="fr-label">Printer</label><PrinterPicker/></div>;
}

const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? '⌘' : 'Ctrl+';

function DropZone() {
  return (
    <div className="fr-drop">
      <div className="fr-drop-title">Drop a model</div>
      <div className="fr-actions">
        <button type="button" className="btn-primary fr-add" onClick={openFileDialog}>
          Add model<kbd>{MOD}I</kbd>
        </button>
        <button type="button" className="btn-ghost" onClick={openProjectDialog}>Open project</button>
      </div>
      <div className="fr-samples">
        <span>or try</span>
        {SAMPLES.map((s) => (
          <button key={s.file} type="button" className="fr-sample" onClick={() => void importFetched(s.label, [[s.url, s.file]])}>
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
    if(documentGeneration()!==generation || !preferences.hydrated || s.profileLibraryLoad.state!=='ready' || preferences.state==='failed' || s.job.state!=='idle'
      || !completed && (preferenceText(SETUP_KEY) || preferenceText('orca.printer'))) { stop(); return; }
    if(s.objects.length && !completed){completed=true;savePreferences({[SETUP_KEY]:'1'});root.dataset.nudge='slice';}
    else if(completed && !s.objects.length)stop();
  };
  const unsubscribe=subscribeStore(check);
  firstVisitObserver=stop;
}

function Onboarding({ done }: { done(): void }) {
  const [step, setStep] = useState<0 | 1>(0);
  const ready = useStore(printerReady);
  return (
    <div className="fr-onboard">
      <div className="fr-steps" aria-label={`Step ${step + 1} of 2`}><span className="on" /><span className={step ? 'on' : ''} /></div>
      {step === 0 ? <PrinterChoice /> : <div className="fr-printer"><label className="fr-label">Filament</label><FilamentPicker slot={0} /></div>}
      <div className="fr-onboard-foot">
        {step === 1 && <button type="button" className="btn-ghost" onClick={() => setStep(0)}>Back</button>}
        <button type="button" className="btn-primary" disabled={!ready} onClick={() => { if (step === 0) setStep(1); else { savePreferences({ [SETUP_KEY]: '1' }); done(); } }}>{step === 0 ? 'Next' : 'Start printing'}</button>
      </div>
    </div>
  );
}

export function StartOver({ className = 'btn' }: { className?: string }) {
  if (!useDevReset()) return null;
  return <button type="button" className={`fr-startover ${className}`} onClick={() => void startOver().catch(() => {})}>Start over</button>;
}

export default function EmptyPlate() {
  const preferences=useSyncExternalStore(subscribeSavedPreferences,savedPreferencesSnapshot);
  const setup=usePreference(SETUP_KEY),printer=usePreference('orca.printer');
  const library=useStore(s=>s.profileLibraryLoad.state);
  const firstVisit=preferences.hydrated && library==='ready' && preferences.state!=='failed' && !setup && !printer;
  useEffect(()=>{if(firstVisit)afterFirstVisit();},[firstVisit]);
  const [onboarding,setOnboarding]=useState(false);
  useEffect(()=>{if(firstVisit)setOnboarding(true);},[firstVisit]);
  const walking=firstVisit||onboarding;
  return (
    <div className="empty-plate">
      <div className="fr-card">{walking ? <Onboarding done={() => setOnboarding(false)} /> : <DropZone />}</div>
      {!walking && <StartOver />}
    </div>
  );
}

export function SetupWizard({ close }: { close(): void }) {
  const count = useStore((s) => s.objects.length);
  const start = useRef(count);
  useEffect(() => { if (count > start.current) close(); }, [count]);
  return (
    <Modal size="m" label="Get started" onClose={close} returnFocus=".topbar .menu-title">
      <ModalTitle title="Get started" onClose={close} />
      <ModalBody className="setup-wizard-body">
        <PrinterChoice />
        <DropZone />
      </ModalBody>
      <ModalFooter><button type="button" className="btn" onClick={close}>Done</button></ModalFooter>
    </Modal>
  );
}
