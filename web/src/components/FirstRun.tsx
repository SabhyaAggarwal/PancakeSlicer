import { preferenceText, savePreferences, savedPreferencesSnapshot, subscribeSavedPreferences, usePreference } from '../saved-preferences';
import { useEffect, useSyncExternalStore } from 'react';
import knobUrl from '../../../tests/data/nonplanar/corpus/w_knob_knurled.stl?url';
import hookUrl from '../samples/shelf_hook.stl?url';
import { openFileDialog, openProjectDialog } from '../actions';
import { handyUrl, importFetched } from '../plater/handy';
import { getState, resolvePreset, subscribeStore, useStore } from '../state/store';
import { startOver, useDevReset } from '../dev-reset';
import { openSetup } from '../onboarding/setup-flow';
import { documentGeneration, onLeaveDocument } from '../state/history';
import './first-run.css';

const SETUP_KEY = 'orca.setup';
const SAMPLES = [
  { url: handyUrl('3DBenchy.drc'), file: '3DBenchy.drc', label: 'Benchy' },
  { url: knobUrl, file: 'knob.stl', label: 'Knob' },
  { url: hookUrl, file: 'shelf_hook.stl', label: 'Shelf hook' },
];


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

const hasFilament = (s: ReturnType<typeof getState>) => !!s.filamentSlots[0] && (() => { const r = resolvePreset(s, 'filament', s.filamentSlots[0]); return !!r.name && !r.error; })();

export function StartOver({ className = 'btn' }: { className?: string }) {
  if (!useDevReset()) return null;
  return <button type="button" className={`fr-startover ${className}`} onClick={() => void startOver().catch(() => {})}>Start over</button>;
}

let setupOffered = false;
export default function EmptyPlate() {
  const preferences=useSyncExternalStore(subscribeSavedPreferences,savedPreferencesSnapshot);
  const setup=usePreference(SETUP_KEY),printer=usePreference('orca.printer');
  const library=useStore(s=>s.profileLibraryLoad.state);
  const filament=useStore(hasFilament);
  const firstVisit=preferences.hydrated && library==='ready' && preferences.state!=='failed' && !setup && (!printer || !filament);
  useEffect(()=>{if(firstVisit)afterFirstVisit();},[firstVisit]);
  useEffect(()=>{if(firstVisit && !setupOffered){setupOffered=true;openSetup();}},[firstVisit]);
  return (
    <div className="empty-plate">
      <div className="fr-card"><DropZone /></div>
      <StartOver />
    </div>
  );
}

export function SetupWizard({ close }: { close(): void }) {
  useEffect(() => { openSetup(); close(); }, []);
  return null;
}
