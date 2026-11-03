import { nativeSurfaceSummary } from '../preview/explain-record';
import { lazy, Suspense, useState } from 'react';
import { Tabs } from 'radix-ui';
import type { PresetType } from '../api/types';
import { effectiveConfig, printerPreset, setState, useStore } from '../state/store';
import { filamentSlots as capacity } from '../profiles/machines';
import { addFilament, removeFilament } from '../profiles/SetupRows';
import { PresetCombo } from './PresetCombo';
import { PresetActions, ChangesMark } from '../profiles/PresetBar';
import { SettingsTab } from './SettingsTab';
import { surfaceOf } from '../intent/surface';
import './settings.css';
const ObjectList = lazy(() => import('./ObjectList').then(m => ({default: m.ObjectList})));
const ObjectSettings = lazy(() => import('./ObjectSettings').then(m => ({default: m.ObjectSettings})));
const TransformPanel = lazy(() => import('./Transform').then(m => ({default: m.TransformPanel})));

function ProfileSection({type, title}: {type: PresetType; title: string}) {
  const [expanded, setExpanded] = useState(false);
  const machine = useStore(printerPreset);
  const slots = useStore(s => s.filamentSlots);
  const multi = type === 'filament' && capacity(machine) > 1;
  const [slot, setSlot] = useState(0);
  const active = Math.min(slot, Math.max(0, slots.length - 1));
  return <section className={`side-section profile-section ${expanded ? 'is-expanded' : ''}`} aria-label={`${title} profile`}>
    <div className="side-title"><button className="profile-heading-button" aria-expanded={expanded} onClick={() => setExpanded(!expanded)}><span aria-hidden>{expanded ? '▾' : '▸'}</span>{title}</button><ChangesMark type={type} slot={active} /><span className="side-title-tools">{multi && <><button type="button" className="btn" disabled={slots.length >= capacity(machine)} title="All available filament slots are in use" onClick={addFilament}>Add slot</button>{slots.length > 1 && <button type="button" className="btn" onClick={removeFilament}>Remove slot</button>}<button type="button" className="btn" disabled={slots.length < 2} title="Choose two or more filaments to edit flushing volumes" onClick={() => setState({flushDialog: true})}>Flush</button></>}<button type="button" className="btn" onClick={() => setExpanded(!expanded)}>{expanded ? 'Hide settings' : 'Settings'}</button></span></div>
    <div className="side-content"><div className="combo-row"><PresetCombo type={type} slot={active}/><PresetActions type={type} slot={active}/></div>{multi && slots.length > 1 && <div className="seg slot-tabs" aria-label="Filament slots">{slots.map((name, i) => <button key={i} type="button" aria-pressed={i === active} title={name} onClick={() => setSlot(i)}>Slot {i + 1}</button>)}</div>}</div>
    {expanded && <div className="profile-settings"><SettingsTab type={type} slot={active} compact hidePreset /></div>}
  </section>;
}
function ProcessHeader() {
  const mode = useStore(s => s.uiMode), scope = useStore(s => s.processScope), hasObjects = useStore(s => !!s.objects.length);
  return <div className="side-title process-title"><span className="side-title-text">Process</span><Tabs.Root value={scope} onValueChange={v => setState(s => ({processScope: v as 'global' | 'objects', selected: v === 'objects' ? s.selected ?? (s.objects[0] ? {object: s.objects[0].id, instance: s.objects[0].instances[0].id} : null) : s.selected}))}><Tabs.List className="seg" aria-label="Settings scope"><Tabs.Trigger value="global">Global</Tabs.Trigger><Tabs.Trigger value="objects" disabled={!hasObjects} title={!hasObjects ? "Add an object to edit its settings" : "Edit the selected object settings"}>Objects</Tabs.Trigger></Tabs.List></Tabs.Root><span className="side-title-tools"><Tabs.Root value={mode} onValueChange={v => setState({uiMode: v as typeof mode})}><Tabs.List className="seg" aria-label="Settings visibility">{(['simple','advanced','develop'] as const).map((v,i) => <Tabs.Trigger key={v} value={v}>{['Simple','Advanced','Expert'][i]}</Tabs.Trigger>)}</Tabs.List></Tabs.Root></span></div>;
}
function EngineLine() {
  const job = useStore(s => s.job), state = useStore(s => s);
  const mode = surfaceOf(effectiveConfig(state, 'process'));
  const record = job.state === 'done' && !job.stale ? job.status?.record : undefined;
  const summary = record ? nativeSurfaceSummary(record) : null;
  const engine = state.api?.mock ? 'Demo engine' : 'Native engine';
  const line = summary ?? (job.state === 'done' && !job.stale ? 'Slice complete · no curved coverage reported' : mode === 'smooth' ? `Curved top · ${engine} · not sliced` : `Flat layers · ${engine}`);
  return <div className="engine-line" role="status">{line}</div>;
}
export function Sidebar() {
  const hasObjects = useStore(s => !!s.objects.length), objects = useStore(s => s.processScope === 'objects' && !!s.objects.length);
  return <aside className="sidebar"><ProfileSection type="machine" title="Printer"/><ProfileSection type="filament" title="Filament"/><ProcessHeader/><div className="objects-region">{hasObjects ? <Suspense fallback={null}><ObjectList /></Suspense> : <div className="object-empty">Objects</div>}</div>{hasObjects && <Suspense fallback={null}><TransformPanel/></Suspense>}<div className="process-panel">{objects ? <Suspense fallback={null}><ObjectSettings/></Suspense> : <SettingsTab type="process" compact/>}</div><EngineLine/></aside>;
}
