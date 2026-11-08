import { openUrlImport } from '../url-import';
import { UrlImportInline } from './UrlImport';
import { preferenceText, savePreferences, savedPreferencesSnapshot, subscribeSavedPreferences, usePreference } from '../saved-preferences';
import { useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { HoverCard } from 'radix-ui';
import { openFileDialog, openProjectDialog } from '../actions';
import { importFetched } from '../plater/handy';
import { getState, resolvePreset, subscribeStore, toast, useStore } from '../state/store';
import benchyModel from '../samples/benchy.drc?url';
import benchyPreview from '../samples/benchy.webp?url';
import wavesModel from '../samples/serpentine_waves.drc?url';
import wavesPreview from '../samples/serpentine_waves.webp?url';
import pyramidsModel from '../samples/four_pyramids.drc?url';
import pyramidsPreview from '../samples/four_pyramids.webp?url';
import ridgesModel from '../samples/chevron_ridges.drc?url';
import ridgesPreview from '../samples/chevron_ridges.webp?url';
import ripplesModel from '../samples/ripple_cylinder.drc?url';
import ripplesPreview from '../samples/ripple_cylinder.webp?url';
import cylinderModel from '../samples/wavy_cylinder.drc?url';
import cylinderPreview from '../samples/wavy_cylinder.webp?url';
import { openSetup } from '../onboarding/setup-flow';
import { documentGeneration, onLeaveDocument } from '../state/history';
import './first-run.css';

const SETUP_KEY = 'orca.setup';
const SAMPLES: { file: string; label: string; model: string; preview: string }[] = [
  { file: '3DBenchy.drc', label: 'Benchy', model: benchyModel, preview: benchyPreview },
  { file: 'serpentine_waves.drc', label: 'Waves', model: wavesModel, preview: wavesPreview },
  { file: 'four_pyramids.drc', label: 'Pyramids', model: pyramidsModel, preview: pyramidsPreview },
  { file: 'chevron_ridges.drc', label: 'Ridges', model: ridgesModel, preview: ridgesPreview },
  { file: 'ripple_cylinder.drc', label: 'Ripples', model: ripplesModel, preview: ripplesPreview },
  { file: 'wavy_cylinder.drc', label: 'Cylinder', model: cylinderModel, preview: cylinderPreview },
];
const models = new Map<string, Promise<string>>();
function modelBlobUrl(url: string) {
  let p = models.get(url);
  if (!p) {
    p = fetch(url).then(async (r) => {
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
      return URL.createObjectURL(await r.blob());
    });
    p.catch(() => models.delete(url));
    models.set(url, p);
  }
  return p;
}
let warmed = false;
function warmSamples() {
  if (warmed) return;
  warmed = true;
  for (const s of SAMPLES) { const img = new Image(); img.src = s.preview; void img.decode().catch(() => {}); }
  const idle = window.requestIdleCallback ?? ((f: () => void) => setTimeout(f, 200));
  idle(() => { for (const s of SAMPLES) modelBlobUrl(s.model).catch(() => {}); });
}
function Sample({ file, label, model, preview }: (typeof SAMPLES)[number]) {
  const [open, setOpen] = useState(false), [busy, setBusy] = useState(false), picked = useRef(false);
  const pick = async () => {
    picked.current = true; setOpen(false);
    if (busy) return;
    setBusy(true);
    try {
      await importFetched(label, [[await modelBlobUrl(model), file]]);
    } catch (e) {
      toast(`${label}: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };
  return <HoverCard.Root open={open} onOpenChange={(v) => setOpen(v && !picked.current)} openDelay={30} closeDelay={60}>
    <HoverCard.Trigger asChild>
      <button type="button" className="fr-sample" data-busy={busy || undefined} aria-busy={busy || undefined}
        onPointerLeave={() => { picked.current = false; }} onBlur={() => { picked.current = false; }} onClick={() => void pick()}>{label}</button>
    </HoverCard.Trigger>
    <HoverCard.Portal>
      <HoverCard.Content className="fr-preview" side="top" sideOffset={8} collisionPadding={8} aria-hidden>
        <div className="sample-preview" style={{ backgroundImage: `url(${preview})` }} />
      </HoverCard.Content>
    </HoverCard.Portal>
  </HoverCard.Root>;
}


const MOD = /Mac|iPhone|iPad/.test(navigator.platform) ? '⌘' : 'Ctrl+';

function DropZone() {
  useEffect(warmSamples, []);
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
