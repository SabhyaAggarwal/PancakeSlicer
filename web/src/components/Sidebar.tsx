import { OPTIONS, type OptionMode } from '../options';
import { compatible, getState, markStale, printerPreset, setState, useStore } from '../state/store';
import { undoable } from '../state/history';
import { lazy, Suspense, useState } from 'react';
import { flushModified } from '../flush-state';
import { ManipulationPanel } from './CanvasOverlays';
import { Icon, IconButton } from './Icon';
import { PresetCombo } from './PresetCombo';
import { SettingsTab } from './SettingsTab';
import { filamentColor } from '../plater/filamentColor';
import { readReport } from '../preview/explain-record';
import { jumpToMove, streamLayerOf } from '../preview/jump';
import { shortTime } from '../preview/format';

const ObjectList = lazy(() => import('./ObjectList').then((m) => ({ default: m.ObjectList })));

const ObjectSettings = lazy(() => import('./ObjectSettings').then((m) => ({ default: m.ObjectSettings })));

function SectionTitle({ icon, title, children }: { icon: string; title: string; children?: React.ReactNode }) {
  return (
    <div className="side-title">
      <Icon name={icon} size={20} />
      <span className="side-title-text">{title}</span>
      <span className="side-title-tools">{children}</span>
    </div>
  );
}

function headBoxLabel(name: string) {
  const words = name.replace(/\[[^\]]*\]/g, ' ').split(/[_ ]+/).filter((w) => w && !/\d/.test(w));
  return words.length ? words.join(' ') : name;
}

type VerifierHit = { move_id?: number; gcode_line?: number; layer?: number; penetration_mm?: number; component?: string | null };

function ClearanceWarning() {
  const job = useStore((s) => s.job);
  const v = job.state === 'done' && !job.stale ? (job.status?.record?.totals as { verifier?: Record<string, unknown> } | undefined)?.verifier : undefined;
  const worst = (v?.worst ?? []) as VerifierHit[];
  const hits = (v?.first_hits ?? []) as VerifierHit[];
  if (!v?.ran || !(Number(v.hit_moves) > 0) || !(worst.length || hits.length)) return null;
  const hit = worst[0] ?? hits.reduce((a, b) => ((b.penetration_mm ?? 0) > (a.penetration_mm ?? 0) ? b : a));
  const layer = hit.layer ?? (hit.move_id === undefined ? null : streamLayerOf(job.id, hit.move_id));
  const text = `Moves that would hit printed material: ${v.hit_moves} (worst clearance: ${Number(v.min_clearance_mm).toFixed(3)} mm, ` +
    `${hit.component ? headBoxLabel(hit.component) + ', ' : ''}${layer !== null ? `layer ${layer + 1}, ` : ''}line ${hit.gcode_line}).`;
  return (
    <div className="side-section clearance-warning">
      <div className="clearance-warning-text">{text}</div>
      {hit.move_id !== undefined && <button className="btn" onClick={() => jumpToMove(hit.move_id!)}>Jump to move</button>}
    </div>
  );
}

function NonplanarResult() {
  const job = useStore((s) => s.job);
  const heads = useStore((s) => s.heads);
  const [open, setOpen] = useState(true);
  const report = job.state === 'done' && !job.stale ? readReport(job.status?.record) : null;
  if (!report) return null;
  const labels = OPTIONS.nonplanar_mode;
  const modeLabel = (m: string) => labels?.enum_labels?.[labels.enum_values?.indexOf(m) ?? -1] ?? m;
  const head = heads?.find((h) => h.id === report.head);
  const lines: string[] = [];
  if (head) lines.push(`Head: ${report.head} (${head.calibrated ? 'calibrated' : 'uncalibrated'}).`);
  lines.push(!report.verified ? 'Clearance check: not run.' : report.hits === 0 ? 'Clearance check: no move hits printed material.'
    : `Clearance check: ${report.hits} ${report.hits === 1 ? 'move would' : 'moves would'} hit printed material.`);
  if (report.timeS > 0) lines.push(`Curved passes: ${shortTime(report.curvedS)} of the ${shortTime(report.timeS)} print.`);
  for (const o of report.objects) {
    if (o.topPatches + o.otherPatches === 0 && o.planarTop + o.flatTop === 0) continue;
    lines.push('', `${o.name} (${modeLabel(o.mode)})`);
    const share = o.share ?? (o.curvedTop + o.planarTop > 0 ? o.curvedTop / (o.curvedTop + o.planarTop) : 1);
    lines.push(o.topPatches > 0 ? `Curved top: ${(100 * share).toFixed(1)} % printed curved, patches: ${o.topPatches}, height bands: ${Math.max(o.bands, 1)}.` : 'No curved top.');
    if (o.otherPatches > 0) lines.push(`Curved undersides, thin parts and bridges: ${o.otherPatches}.`);
    if (o.planarTop > 0) lines.push(`Planar fallback: ${o.planarTop.toFixed(1)} mm², mostly ${o.planarReason}.`);
    if (o.flatTop > 0) lines.push(`Planar by design: ${o.flatTop.toFixed(1)} mm², mostly ${o.flatReason}.`);
    if (o.supportFree && o.supportFree > 0) lines.push(`Support skipped under ${o.supportFree.toFixed(0)} mm² of overhang the curved layers print.`);
    if (o.orientation) lines.push(`Orientation: ${o.orientation}.`);
  }
  return (
    <div className="side-section np-result">
      <div className="side-title" role="button" tabIndex={0} aria-expanded={open} onClick={() => setOpen(!open)} onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && setOpen(!open)}>
        <Icon name="param_nonplanar" size={20} />
        <span className="side-title-text">Non-planar</span>
      </div>
      {open && <div className="np-result-text">{lines.join('\n')}</div>}
    </div>
  );
}

function PrinterSection() {
  const machine = useStore((s) => printerPreset(s));
  const bedType = useStore((s) => s.bedType);
  const bedDef = OPTIONS.curr_bed_type;
  const cover = machine?.model ? `/profiles/${machine.vendor}/${encodeURIComponent(machine.model)}_cover.png` : '/images/printer_placeholder.png';
  const nozzle = machine?.config.nozzle_diameter;
  const nozzleText = Array.isArray(nozzle) ? nozzle[0] : nozzle;
  const multiBed = machine?.config.support_multi_bed_types;
  const showBed = (machine?.vendor === 'BBL' || multiBed === true || multiBed === 1 || multiBed === '1') && !!bedDef?.enum_values;
  return (
    <div className="side-section">
      <SectionTitle icon="printer" title="Printer" />
      <div className="side-content printer-content">
        <img className="printer-cover" src={cover} alt="" onError={(e) => ((e.target as HTMLImageElement).src = '/images/printer_placeholder.png')} />
        <div className="printer-fields">
          <div className="combo-row">
            <PresetCombo type="machine" />
            <IconButton icon="edit" title="Click to edit preset" onClick={() => setState({ dialog: { type: 'machine', slot: 0 } })} />
          </div>
          <div className="printer-sub">
            <span className="chip" title="Nozzle diameter">
              Nozzle {nozzleText ?? '—'} mm
            </span>
            {showBed && (
              <select className="combo bed-combo" title="Plate type" value={bedType} onChange={(e) => undoable('Change plate type', () => setState({ bedType: e.target.value }))}>
                {bedDef!.enum_values!.map((v, i) =>
                  machine?.not_support_bed_type?.some((b) => b === v || b === bedDef!.enum_labels?.[i]) ? null : (
                    <option key={v} value={v}>
                      {bedDef!.enum_labels?.[i] ?? v}
                    </option>
                  ),
                )}
              </select>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

function FilamentSection() {
  const slots = useStore((s) => s.filamentSlots);
  useStore((s) => s.filamentEdits);
  useStore((s) => s.slotColours);
  useStore((s) => s.projectMaterials);
  const s = getState();
  const add = () =>
    undoable('Add filament', () => setState((st) => {
      const fils = compatible(st, 'filament');
      const last = st.filamentSlots[st.filamentSlots.length - 1] ?? fils[0]?.name ?? '';
      const slotColours = [...st.slotColours];
      slotColours[st.filamentSlots.length] = NEXT_COLOURS[nextColour++ % NEXT_COLOURS.length];
      return { filamentSlots: [...st.filamentSlots, last], slotColours };
    }));
  const remove = () =>
    undoable('Remove filament', () => setState((st) => ({
      filamentSlots: st.filamentSlots.length > 1 ? st.filamentSlots.slice(0, -1) : st.filamentSlots,
      slotColours: st.slotColours.slice(0, st.filamentSlots.length - 1),
      objects: st.objects.map((o) => (o.extruder >= st.filamentSlots.length ? { ...o, extruder: 1 } : o)),
      events: st.events.filter((e) => e.type !== 'change' || Number(e.text) < st.filamentSlots.length),
    })));
  const flushChanged = useStore((st) => flushModified(st));
  const pickColour = (i: number, colour: string) =>
    undoable('Change filament colour', () => setState((st) => {
      const slotColours = [...st.slotColours];
      slotColours[i] = colour.toUpperCase();
      return { slotColours, job: markStale(st.job) };
    }), `colour:${i}`);
  return (
    <div className="side-section">
      <SectionTitle icon="filament" title="Filament">
        {slots.length > 1 && (
          <button
            type="button"
            className={`flush-btn ${flushChanged ? 'modified' : ''}`}
            title={flushChanged ? 'The flushing volumes were changed from the values calculated from the filament colours' : 'Edit the flushing volumes between filaments'}
            onClick={() => setState({ flushDialog: true })}
          >
            Flushing volumes
          </button>
        )}
        {slots.length > 1 && <IconButton icon="delete_filament" title="Remove last filament" onClick={remove} />}
        <IconButton icon="add_filament" title="Add one filament" onClick={add} disabled={slots.length >= 64} />
      </SectionTitle>
      <div className="side-content filament-list">
        {slots.map((_, i) => (
          <div key={i} className="filament-row">
            <label className="filament-swatch" style={{ background: filamentColor(s, i + 1) }} title="Click to select filament color">
              {i + 1}
              <input type="color" value={filamentColor(s, i + 1)} onChange={(e) => pickColour(i, e.target.value)} />
            </label>
            <PresetCombo type="filament" slot={i} />
            <IconButton icon="edit" title="Click to edit preset" onClick={() => setState({ dialog: { type: 'filament', slot: i } })} />
          </div>
        ))}
      </div>
    </div>
  );
}

const MODES: OptionMode[] = ['simple', 'advanced', 'develop'];
const MODE_TIPS = ['Simple settings', 'Advanced settings', 'Expert settings'];

function ProcessHeader() {
  const uiMode = useStore((s) => s.uiMode);
  const scope = useStore((s) => s.processScope);
  const hasObjects = useStore((s) => s.objects.length > 0);
  const toObjects = () =>
    setState((s) => ({
      processScope: 'objects',
      selected: s.selected ?? (s.objects[0] ? { object: s.objects[0].id, instance: s.objects[0].instances[0].id } : null),
    }));
  return (
    <div className="side-title process-title">
      <Icon name="process" size={20} />
      <span className="side-title-text">Process</span>
      <span className="mode-switch">
        <button className={scope === 'global' ? 'active' : ''} onClick={() => setState({ processScope: 'global' })}>
          Global
        </button>
        <button className={scope === 'objects' ? 'active' : ''} disabled={!hasObjects} onClick={toObjects} title="Settings of the selected object">
          Objects
        </button>
      </span>
      <span className="side-title-tools">
        <IconButton icon="advanced" title="Cycle settings visibility" onClick={() => setState({ uiMode: MODES[(MODES.indexOf(uiMode) + 1) % 3] })} />
        <span className="mode-view" role="radiogroup">
          {MODES.map((m, i) => (
            <button key={m} type="button" role="radio" aria-checked={m === uiMode} className={m === uiMode ? 'active' : ''} title={MODE_TIPS[i]} onClick={() => setState({ uiMode: m })} />
          ))}
        </span>
      </span>
    </div>
  );
}

const NEXT_COLOURS = ['#00C1AE', '#F4E2C1', '#ED1C24', '#00FF7F', '#F26722', '#FFEB31', '#7841CE', '#115877',
  '#ED1E79', '#2EBDEF', '#345B2F', '#800080', '#FA8173', '#800000', '#F7B763', '#A4C41E'];
let nextColour = 0;

export function Sidebar() {
  const hasObjects = useStore((s) => s.objects.length > 0);
  const objects = useStore((s) => s.processScope === 'objects' && s.objects.length > 0);
  return (
    <aside className="sidebar">
      <ClearanceWarning />
      <NonplanarResult />
      <PrinterSection />
      <FilamentSection />
      <ProcessHeader />
      {hasObjects && <Suspense fallback={null}><ObjectList /></Suspense>}
      {hasObjects && <ManipulationPanel docked />}
      <div className="process-panel">
        {objects ? (
          <Suspense fallback={<div role="status">Loading object settings…</div>}>
            <ObjectSettings />
          </Suspense>
        ) : <SettingsTab type="process" compact />}
      </div>
    </aside>
  );
}
