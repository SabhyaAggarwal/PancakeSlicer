import { nativeSurfaceSummary } from '../preview/explain-record';
import { lazy, Suspense } from 'react';
import { Tabs } from 'radix-ui';
import { Dropdown } from './Dropdown';
import type { PresetType } from '../api/types';
import { effectiveConfig, printerPreset, setState, useStore } from '../state/store';
import { filamentSlots as capacity } from '../profiles/machines';
import { addFilament, removeSlot } from '../profiles/SetupRows';
import { PresetCombo } from './PresetCombo';
import { PresetBar } from '../profiles/PresetBar';
import { SettingsTab } from './SettingsTab';
import { surfaceOf } from '../intent/surface';
import './settings.css';
const ObjectSettings = lazy(() => import('./ObjectSettings').then(m => ({default: m.ObjectSettings})));

function ProfileRow({type, slot = 0, title, remove}: {type: PresetType; slot?: number; title: string; remove?: () => void}) {
  const name = useStore(s => type === 'machine' ? s.printer : s.filamentSlots[slot]);
  return <div className="profile-selector-row"><span className="profile-role">{title}</span><PresetCombo type={type} slot={slot}/><button type="button" className="profile-settings-action btn" onClick={() => setState({presetUx: {kind: 'presets', type, name, edit: true}})}>Settings</button>{remove && <button type="button" className="filament-remove icon-btn" aria-label={`Remove filament slot ${slot + 1}`} title={`Remove filament slot ${slot + 1}`} onClick={remove}>×</button>}</div>;
}
function PrinterMaterials() {
  const machine = useStore(printerPreset), slots = useStore(s => s.filamentSlots);
  const lastOnly = useStore(s => !!Object.keys(s.mixes).length || s.objects.some(o => o.paint?.color || o.volumes?.some(v => v.paint?.color)));
  const multi = capacity(machine) > 1;
  return <section className="profile-section printer-materials" aria-label="Printer and its filaments">
    <ProfileRow type="machine" title="Printer"/>
    <div className="printer-filaments">{slots.map((_name, i) => <ProfileRow key={i} type="filament" slot={i} title={slots.length > 1 ? `Filament ${i + 1}` : 'Filament'} remove={multi && slots.length > 1 && (!lastOnly || i === slots.length - 1) ? () => removeSlot(i) : undefined}/>)}</div>
    {multi && slots.length < capacity(machine) && <button type="button" className="btn-ghost add-material-slot" onClick={addFilament}>Add slot</button>}
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
    <PrinterMaterials/>
    <section className="process-section" aria-label="Process profile and settings">
      <div className="side-title"><span className="side-title-text">Print settings</span></div>
      <PresetBar type="process"/>
      <ProcessScope/>
      <div className="process-panel">{objects ? <Suspense fallback={null}><ObjectSettings/></Suspense> : <SettingsTab type="process" compact hidePreset/>}</div>
    </section>
    <EngineLine/>
  </aside>;
}
