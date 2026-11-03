import { ProfileLibraryNotice } from './ProfileLibraryNotice';
import { nativeSurfaceSummary } from '../preview/explain-record';
import { lazy, Suspense, useState, type ReactNode } from 'react';
import { Tabs } from 'radix-ui';
import type { PresetType } from '../api/types';
import { printerPreset, resolvePreset, setState, useStore } from '../state/store';
import { printerReady } from '../workspace-sheet';
import { filamentSlots as capacity } from '../profiles/machines';
import { addFilament, removeSlot } from '../profiles/SetupRows';
import { PresetCombo } from './PresetCombo';
import { ChangesMark, ConflictLine, PresetActions } from '../profiles/PresetBar';
import { IconButton } from './Icon';
import { profileSummary,printerCover } from '../profiles/metadata';
import './settings.css';
const ObjectSettings = lazy(() => import('./ObjectSettings').then(m => ({default: m.ObjectSettings})));
const SettingsTab = lazy(() => import('./SettingsTab').then(m => ({default: m.SettingsTab})));

function RailSection({title, actions, className = '', children}: {title: string; actions?: ReactNode; className?: string; children: ReactNode}) {
  return <section className={`rail-section ${className}`} aria-label={title}>
    <header className="rail-head"><h2 className="rail-title">{title}</h2>{actions && <div className="rail-actions">{actions}</div>}</header>
    {children}
  </section>;
}
const edit = (type: PresetType, name?: string) => setState({presetUx: {kind: 'presets', type, ...(name ? {name, edit: true} : {})}});

function PrinterSection() {
  const machine = useStore(printerPreset), name = useStore(s => s.printer), ready = useStore(printerReady);
  const [failedCover, setFailedCover] = useState<string>();
  const cover = ready && machine ? printerCover(machine.vendor, machine.config, machine.model) : undefined;
  return <RailSection title="Printer" actions={<IconButton icon="edit" title="Edit printer" disabled={!ready} onClick={() => edit('machine', name)}/>}>
    <div className="rail-row">{cover && failedCover !== cover && <img className="rail-cover" src={cover} alt="" loading="lazy" width={44} height={44} onError={() => setFailedCover(cover)}/>}<PresetCombo type="machine"/></div>
    {ready && machine && <div className="rail-row"><span className="rail-sub">{profileSummary('machine', machine.config)}</span></div>}
  </RailSection>;
}
function FilamentSection() {
  const machine = useStore(printerPreset), slots = useStore(s => s.filamentSlots);
  const only = useStore(s => s.filamentSlots.length === 1 ? s.filamentSlots[0] : undefined);
  const onlyError = useStore(s => s.filamentSlots.length === 1 && !!resolvePreset(s, 'filament', s.filamentSlots[0]).error);
  const max = capacity(machine);
  return <RailSection title="Filament" actions={<>
    {max > 1 && slots.length < max && <IconButton icon="add_filament" title="Add filament slot" onClick={addFilament}/>}
    {max > 1 && slots.length > 1 && <IconButton icon="delete_filament" title={`Remove filament slot ${slots.length}`} onClick={() => removeSlot(slots.length - 1)}/>}
    <IconButton icon="edit" title="Edit filament" disabled={onlyError} onClick={() => edit('filament', only)}/>
  </>}>
    {slots.map((_name, i) => <div key={i} className="rail-row"><span className="rail-index">{i + 1}</span><PresetCombo type="filament" slot={i}/></div>)}
  </RailSection>;
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
export function Sidebar() {
  const objects = useStore(s => s.processScope === 'objects' && !!s.objects.length);
  const ready = useStore(printerReady), process = useStore(s => s.process);
  return <aside className="sidebar">
    <ProfileLibraryNotice/>
    <PrinterSection/>
    {ready && <>
      <FilamentSection/>
      <RailSection title="Print settings" className="process-section" actions={<><ChangesMark type="process"/><IconButton icon="edit" title="Edit print settings" disabled={!process} onClick={() => edit('process', process)}/><PresetActions type="process"/></>}>
        <div className="rail-row"><PresetCombo type="process"/></div>
        {process && <ConflictLine type="process" name={process}/>}
        <ProcessScope/>
        <div className="process-panel"><Suspense fallback={<div className="settings-load" role="status">Loading settings…</div>}>{objects ? <ObjectSettings/> : <SettingsTab type="process" compact hidePreset/>}</Suspense></div>
      </RailSection>
      <SliceSummary/>
    </>}
  </aside>;
}
