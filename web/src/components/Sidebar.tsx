import { ProfileLibraryNotice } from './ProfileLibraryNotice';
import { nativeSurfaceSummary } from '../preview/explain-record';
import { lazy, Suspense, useState } from 'react';
import { Tabs } from 'radix-ui';
import type { PresetType } from '../api/types';
import { printerPreset, resolvePreset, setState, useStore } from '../state/store';
import { printerReady } from '../workspace-sheet';
import { filamentSlots as capacity } from '../profiles/machines';
import { addFilament, removeSlot } from '../profiles/SetupRows';
import { PresetCombo } from './PresetCombo';
import { PresetBar } from '../profiles/PresetBar';
import { SectionHeading } from './SectionHeading';
import { profileSummary,printerCover } from '../profiles/metadata';
import './settings.css';
const ObjectSettings = lazy(() => import('./ObjectSettings').then(m => ({default: m.ObjectSettings})));
const SettingsTab = lazy(() => import('./SettingsTab').then(m => ({default: m.SettingsTab})));

function ProfileRow({type, slot = 0, title, remove}: {type: PresetType; slot?: number; title: string; remove?: () => void}) {
  const machine=useStore(printerPreset);
  const error=useStore(s=>resolvePreset(s,type,type === 'machine' ? s.printer : s.filamentSlots[slot]).error);
  const [failedCover,setFailedCover]=useState<string>();
  const cover=type === 'machine' && machine && !error ? printerCover(machine.vendor,machine.config,machine.model) : undefined;
  const name = useStore(s => type === 'machine' ? s.printer : s.filamentSlots[slot]);
  return <div className="profile-selector-row">{type === 'machine' ? cover && failedCover !== cover && <span className="profile-role"><img className="rail-printer-cover" src={cover} alt="" loading="lazy" width={36} height={36} onError={()=>setFailedCover(cover)}/></span> : <span className="profile-role">{title}</span>}<PresetCombo type={type} slot={slot}/><button type="button" className="profile-settings-action btn" disabled={!name || !!error} aria-label={`Edit ${type === 'machine' ? 'printer' : `filament ${slot + 1}`}`} onClick={() => setState({presetUx: {kind: 'presets', type, name, edit: true}})}>Edit</button>{remove && <button type="button" className="filament-remove icon-btn" aria-label={`Remove filament slot ${slot + 1}`} onClick={remove}>×</button>}</div>;
}
function PrinterMaterials() {
  const machine = useStore(printerPreset), slots = useStore(s => s.filamentSlots);
  const lastOnly = useStore(s => !!Object.keys(s.mixes).length || s.objects.some(o => o.paint?.color || o.volumes?.some(v => v.paint?.color)));
  const machineError=useStore(s=>resolvePreset(s,'machine',s.printer).error);
  const multi = !machineError && capacity(machine) > 1;
  return <section className="profile-section printer-materials" aria-label="Printer and its filaments">
    <SectionHeading level="main">Printer</SectionHeading>
    <div className="printer-setup-body">
    <ProfileRow type="machine" title="Printer"/>{machine && !machineError && <div className="printer-hardware-summary">{profileSummary('machine',machine.config)}</div>}
    <section className="printer-filaments" aria-label="Filaments for this printer"><SectionHeading level="sub">Filaments</SectionHeading>{slots.map((_name, i) => <ProfileRow key={i} type="filament" slot={i} title={String(i + 1)} remove={multi && slots.length > 1 && (!lastOnly || i === slots.length - 1) ? () => removeSlot(i) : undefined}/>)}
    {multi && slots.length < capacity(machine) && <button type="button" className="btn-ghost add-material-slot" onClick={addFilament}>Add slot</button>}
    </section></div>
  </section>;
}
function ProcessScope() {
  const scope = useStore(s => s.processScope), hasObjects = useStore(s => !!s.objects.length);
  return <div className="process-scope"><div className="scope-target"><span className="scope-caption">Apply to</span>
    <Tabs.Root value={scope} onValueChange={v => setState(s => ({processScope: v as 'global' | 'objects', selected: v === 'objects' ? s.selected ?? (s.objects[0] ? {object: s.objects[0].id, instance: s.objects[0].instances[0].id} : null) : s.selected}))}><Tabs.List className="seg scope-tabs" aria-label="Settings scope"><Tabs.Trigger value="global">Project</Tabs.Trigger><Tabs.Trigger value="objects" disabled={!hasObjects}>Objects</Tabs.Trigger></Tabs.List></Tabs.Root>
    </div>
  </div>;
}
function SliceSummary() {
  const summary = useStore(s => s.job.state === 'done' && !s.job.stale && s.job.status?.record ? nativeSurfaceSummary(s.job.status.record) : null);
  return summary ? <div className="engine-line" role="status">{summary}</div> : null;
}
function NoPrinter() {
  return <section className="profile-section rail-printer-empty" aria-label="Printer">
    <SectionHeading level="main">Printer</SectionHeading>
    <div className="rail-printer-empty-body"><PresetCombo type="machine"/></div>
  </section>;
}
export function Sidebar() {
  const objects = useStore(s => s.processScope === 'objects' && !!s.objects.length);
  const ready = useStore(printerReady);
  return <aside className="sidebar">
    <ProfileLibraryNotice/>
    {ready ? <>
      <PrinterMaterials/>
      <section className="process-section" aria-label="Process profile and settings">
        <SectionHeading level="main">Print settings</SectionHeading>
        <PresetBar type="process"/>
        <ProcessScope/>
        <div className="process-panel"><Suspense fallback={<div className="settings-load" role="status">Loading settings…</div>}>{objects ? <ObjectSettings/> : <SettingsTab type="process" compact hidePreset/>}</Suspense></div>
      </section>
      <SliceSummary/>
    </> : <NoPrinter/>}
  </aside>;
}
