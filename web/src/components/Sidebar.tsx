import { ProfileLibraryNotice } from './ProfileLibraryNotice';
import { nativeSurfaceSummary } from '../preview/explain-record';
import { lazy, Suspense, useState, type ReactNode } from 'react';
import type { PresetType } from '../api/types';
import { effectiveMode, printerPreset, resolvePreset, setState, useStore } from '../state/store';
import { printerReady } from '../workspace-sheet';
import { filamentSlots as capacity } from '../profiles/machines';
import { addFilament, removeSlot } from '../profiles/SetupRows';
import { PresetCombo } from './PresetCombo';
import { ChangesMark, ConflictLine, PresetActions } from '../profiles/PresetBar';
import { MenuButton, type MenuEntry } from './Menu';
import { Caret } from './Caret';
import { savePreferences } from '../saved-preferences';
import { printerCover } from '../profiles/metadata';
import './settings.css';
import { Glyph, type GlyphName } from './Glyph';
const ObjectSettings = lazy(() => import('./ObjectSettings').then(m => ({default: m.ObjectSettings})));
const SettingsTab = lazy(() => import('./SettingsTab').then(m => ({default: m.SettingsTab})));

function RailSection({title, meta, actions, className = '', children}: {title: string; meta?: ReactNode; actions?: ReactNode; className?: string; children: ReactNode}) {
  return <section className={`rail-section ${className}`} aria-label={title}>
    <header className="rail-head"><h2 className="rail-title">{title}</h2>{meta}{actions && <div className="rail-actions">{actions}</div>}</header>
    {children}
  </section>;
}
function RailAction({glyph, label, disabled, onClick}: {glyph: GlyphName; label: string; disabled?: boolean; onClick(): void}) {
  return <button type="button" className="icon-btn rail-act" aria-label={label} disabled={disabled} onClick={onClick}><Glyph name={glyph}/></button>;
}
const edit = (type: PresetType, name?: string) => setState({presetUx: {kind: 'presets', type, ...(name ? {name, edit: true} : {})}});

function PrinterSection() {
  const machine = useStore(printerPreset), name = useStore(s => s.printer), ready = useStore(printerReady);
  const [failedCover, setFailedCover] = useState<string>();
  const cover = ready && machine ? printerCover(machine.vendor, machine.config, machine.model) : undefined;
  return <RailSection title="Printer" actions={<RailAction glyph="edit" label="Edit printer" disabled={!ready} onClick={() => edit('machine', name)}/>}>
    <div className="rail-row">{cover && failedCover !== cover && <img className="rail-cover" src={cover} alt="" loading="lazy" width={40} height={40} onError={() => setFailedCover(cover)}/>}<PresetCombo type="machine"/></div>
  </RailSection>;
}
function FilamentSection() {
  const machine = useStore(printerPreset), slots = useStore(s => s.filamentSlots);
  const only = useStore(s => s.filamentSlots.length === 1 ? s.filamentSlots[0] : undefined);
  const onlyError = useStore(s => s.filamentSlots.length === 1 && !!resolvePreset(s, 'filament', s.filamentSlots[0]).error);
  const max = capacity(machine);
  return <RailSection title="Filament" actions={<>
    {max > 1 && slots.length < max && <RailAction glyph="plus" label="Add filament slot" onClick={addFilament}/>}
    <RailAction glyph="edit" label="Edit filament" disabled={onlyError} onClick={() => edit('filament', only)}/>
  </>}>
    {slots.map((_name, i) => <div key={i} className="rail-row rail-slot"><PresetCombo type="filament" slot={i}/>
      {slots.length > 1 && <button type="button" className="icon-btn rail-slot-remove" aria-label={`Remove filament slot ${i + 1}`} onClick={() => removeSlot(i)}><Glyph name="close" size={14}/></button>}</div>)}
  </RailSection>;
}
const DETAIL = [['simple', 'Simple'], ['advanced', 'Advanced'], ['develop', 'Expert']] as const;
type Detail = (typeof DETAIL)[number][0];
function useViewChoices() {
  const scope = useStore(s => s.processScope), hasObjects = useStore(s => !!s.objects.length);
  const target = useStore(s => { const sel = s.selected; if (!sel) return ''; if (sel.all) return sel.picked ? `${sel.picked.length} selected` : 'All'; return s.objects.find(o => o.id === sel.object)?.name.replace(/\.(stl|obj|3mf|drc)$/i, '') ?? ''; });
  const mode = useStore(effectiveMode), detail: Detail = mode === 'developer' ? 'develop' : mode;
  const setScope = (v: 'global' | 'objects') => setState(s => ({processScope: v, selected: v === 'objects' ? s.selected ?? (s.objects[0] ? {object: s.objects[0].id, instance: s.objects[0].instances[0].id} : null) : s.selected}));
  const setDetail = (id: Detail) => { savePreferences({'orca.settings_detail': id, 'orca.developer_mode': '0'}); setState({uiMode: id, developerMode: false}); };
  const scopeItems: MenuEntry[] = [{label: 'Project', role: 'menuitemradio', checked: scope === 'global', action: () => setScope('global')}, {label: 'Objects', role: 'menuitemradio', checked: scope === 'objects', action: () => setScope('objects')}];
  const detailItems: MenuEntry[] = DETAIL.map(([id, text]) => ({label: text, role: 'menuitemradio', checked: id === detail, action: () => setDetail(id)}));
  const objects = hasObjects && scope === 'objects';
  return {hasObjects, objects, target, detailLabel: DETAIL.find(([id]) => id === detail)?.[1] ?? 'Simple', scopeItems, detailItems};
}
function ViewChoices() {
  const v = useViewChoices();
  return <>
    <span className="rail-menus">
      {v.hasObjects && <MenuButton className="rail-menu" label={`Apply to: ${v.objects ? 'Objects' : 'Project'}`} items={v.scopeItems}>{v.objects ? 'Objects' : 'Project'}{v.objects && v.target && <span className="rail-menu-sub">{v.target}</span>}<Caret/></MenuButton>}
      <MenuButton className="rail-menu" label={`Settings detail: ${v.detailLabel}`} items={v.detailItems}>{v.detailLabel}<Caret/></MenuButton>
    </span>
    <MenuButton className="icon-btn rail-act rail-menus-folded" label="View options" items={[...(v.hasObjects ? [{label: 'Apply to', header: true} as MenuEntry, ...v.scopeItems, 'sep' as const] : []), {label: 'Detail', header: true}, ...v.detailItems]}><Glyph name="settings"/></MenuButton>
  </>;
}
function SliceSummary() {
  const summary = useStore(s => s.job.state === 'done' && !s.job.stale && s.job.status?.record ? nativeSurfaceSummary(s.job.status.record) : null);
  return summary ? <div className="engine-line" role="status">{summary}</div> : null;
}
function RailSkeleton() {
  const bar = (k: number, wide?: boolean) => <div key={k} className={`rail-row rail-skel${wide ? ' rail-row-wide' : ''}`}><span className="skel-block"/></div>;
  return <div className="rail-skeleton" role="status" aria-label="Loading profiles">
    <RailSection title="Filament">{bar(0)}</RailSection>
    <RailSection title="Print settings" className="process-section">{bar(1, true)}<div className="process-panel">{[2, 3, 4, 5, 6, 7].map(k => <div key={k} className="skel-line"/>)}</div></RailSection>
  </div>;
}
export function Sidebar() {
  const objects = useStore(s => s.processScope === 'objects' && !!s.objects.length);
  const ready = useStore(printerReady), process = useStore(s => s.process);
  const loading = useStore(s => !s.api || s.profileLibraryLoad.state === 'loading');
  return <aside className="sidebar scroll-fade">
    <ProfileLibraryNotice/>
    <PrinterSection/>
    {ready && <>
      <FilamentSection/>
      <RailSection title="Print settings" className="process-section" meta={<ChangesMark type="process"/>} actions={<><ViewChoices/><RailAction glyph="edit" label="Edit print settings" disabled={!process} onClick={() => edit('process', process)}/><PresetActions type="process"/></>}>
        <div className="rail-row rail-row-wide"><PresetCombo type="process"/></div>
        {process && <ConflictLine type="process" name={process}/>}
        <div className="process-panel"><Suspense fallback={<div className="settings-load" role="status">Loading settings…</div>}>{objects ? <ObjectSettings/> : <SettingsTab type="process" compact hidePreset/>}</Suspense></div>
      </RailSection>
      <SliceSummary/>
    </>}
    {!ready && loading && <RailSkeleton/>}
  </aside>;
}
