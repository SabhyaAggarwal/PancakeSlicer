import { nativeSurfaceSummary } from '../preview/explain-record';
import { lazy, Suspense, useState } from 'react';
import { Tabs } from 'radix-ui';
import { Dropdown } from './Dropdown';
import type { PresetType } from '../api/types';
import { effectiveConfig, printerPreset, setState, useStore } from '../state/store';
import { filamentSlots as capacity } from '../profiles/machines';
import { addFilament, removeFilament } from '../profiles/SetupRows';
import { PresetCombo } from './PresetCombo';
import { PresetBar } from '../profiles/PresetBar';
import { SettingsTab } from './SettingsTab';
import { surfaceOf } from '../intent/surface';
import './settings.css';
const ObjectSettings = lazy(() => import('./ObjectSettings').then(m => ({default: m.ObjectSettings})));

function ProfileSection({type, title}: {type: PresetType; title: string}) {
  const machine = useStore(printerPreset);
  const slots = useStore(s => s.filamentSlots);
  const multi = type === 'filament' && capacity(machine) > 1;
  const [slot, setSlot] = useState(0);
  const active = Math.min(slot, Math.max(0, slots.length - 1));
  const name = useStore(s => type === 'machine' ? s.printer : s.filamentSlots[active]);
  return <section className="profile-section compact-profile" aria-label={`${title} profile`}>
    <div className="profile-selector-row"><span className="profile-role">{title}</span><PresetCombo type={type} slot={active}/><button type="button" className="profile-settings-action btn" onClick={() => setState({presetUx: {kind: 'presets', type, name}})}>Settings</button></div>
    {multi && <div className="filament-hardware" aria-label="Filament hardware slots">
      {slots.length > 1 && <div className="seg slot-tabs" aria-label="Filament slots">{slots.map((name, i) => <button key={i} type="button" aria-pressed={i === active} title={name} onClick={() => setSlot(i)}>Slot {i + 1}</button>)}</div>}
      <div className="slot-actions"><button type="button" className="btn-ghost" disabled={slots.length >= capacity(machine)} title={slots.length >= capacity(machine) ? 'All hardware slots are in use' : 'Add a hardware filament slot'} onClick={addFilament}>Add slot</button>{slots.length > 1 && <><button type="button" className="btn-ghost" onClick={removeFilament}>Remove slot</button><button type="button" className="btn-ghost" onClick={() => setState({flushDialog: true})}>Flush volumes</button></>}</div>
    </div>}
  </section>;
}
function ProcessScope() {
  const mode = useStore(s => s.uiMode), scope = useStore(s => s.processScope), hasObjects = useStore(s => !!s.objects.length);
  return <div className="process-scope">
    <Tabs.Root value={scope} onValueChange={v => setState(s => ({processScope: v as 'global' | 'objects', selected: v === 'objects' ? s.selected ?? (s.objects[0] ? {object: s.objects[0].id, instance: s.objects[0].instances[0].id} : null) : s.selected}))}><Tabs.List className="seg scope-tabs" aria-label="Settings scope"><Tabs.Trigger value="global">Global</Tabs.Trigger><Tabs.Trigger value="objects" disabled={!hasObjects} title={!hasObjects ? "Add an object to edit its settings" : "Edit the selected object settings"}>Objects</Tabs.Trigger></Tabs.List></Tabs.Root>
    <Dropdown className="visibility-choice" aria-label="Settings visibility" value={mode} onValueChange={v => setState({uiMode: v as typeof mode})} options={[{value: 'simple', label: 'Simple'}, {value: 'advanced', label: 'Advanced'}, {value: 'develop', label: 'Expert'}]}/>
  </div>;
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
  const objects = useStore(s => s.processScope === 'objects' && !!s.objects.length);
  return <aside className="sidebar">
    <ProfileSection type="machine" title="Printer"/>
    <ProfileSection type="filament" title="Filament"/>
    <section className="process-section" aria-label="Process profile and settings">
      <div className="side-title"><span className="side-title-text">Print settings</span></div>
      <PresetBar type="process"/>
      <ProcessScope/>
      <div className="process-panel">{objects ? <Suspense fallback={null}><ObjectSettings/></Suspense> : <SettingsTab type="process" compact hidePreset/>}</div>
    </section>
    <EngineLine/>
  </aside>;
}
