import { openUrlImport } from '../url-import';
import { UrlImportInline } from './UrlImport';
import { preferenceText, savePreferences, savedPreferencesSnapshot, subscribeSavedPreferences, usePreference } from '../saved-preferences';
import { lazy, Suspense, useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { HoverCard } from 'radix-ui';
import { openFileDialog, openProjectDialog } from '../actions';
import { handyUrl, importFetched } from '../plater/handy';
import { getState, resolvePreset, subscribeStore, useStore } from '../state/store';
import { openSetup } from '../onboarding/setup-flow';
import { documentGeneration, onLeaveDocument } from '../state/history';
import './first-run.css';

const SETUP_KEY = 'orca.setup';
const SAMPLES: { file: string; label: string }[] = [
  { file: '3DBenchy.drc', label: 'Benchy' },
  { file: 'serpentine_waves.drc', label: 'Serpentine waves' },
  { file: 'four_pyramids.drc', label: 'Four pyramids' },
  { file: 'chevron_ridges.drc', label: 'Chevron ridges' },
  { file: 'ripple_cylinder.drc', label: 'Ripple cylinder' },
  { file: 'wavy_cylinder.drc', label: 'Wavy cylinder' },
];
const SamplePreview = lazy(() => import('./SamplePreview'));
function Sample({ file, label }: { file: string; label: string }) {
  const [open, setOpen] = useState(false), picked = useRef(false);
  return <HoverCard.Root open={open} onOpenChange={(v) => setOpen(v && !picked.current)} openDelay={250} closeDelay={80}>
    <HoverCard.Trigger asChild>
      <button type="button" className="fr-sample" onPointerLeave={() => { picked.current = false; }} onBlur={() => { picked.current = false; }}
        onClick={() => { picked.current = true; setOpen(false); void importFetched(label, [[handyUrl(file), file]]); }}>{label}</button>
    </HoverCard.Trigger>
    <HoverCard.Portal>
      <HoverCard.Content className="fr-preview" side="top" sideOffset={8} collisionPadding={8} aria-hidden>
        <Suspense fallback={<div className="sample-preview" />}><SamplePreview url={handyUrl(file)} /></Suspense>
      </HoverCard.Content>
    </HoverCard.Portal>
  </HoverCard.Root>;
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
        <button type="button" className="btn-ghost" onClick={() => openUrlImport('', true)}>From URL</button>
      </div>
      <UrlImportInline />
      <div className="fr-samples">
        <span>or try</span>
        {SAMPLES.map((s) => <Sample key={s.file} {...s} />)}
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
    </div>
  );
}

export function SetupWizard({ close }: { close(): void }) {
  useEffect(() => { openSetup(); close(); }, []);
  return null;
}
