import { OPTIONS } from '../options';
import { compatible, getState, markStale, printerPreset, setState, useStore } from '../state/store';
import { flushModified } from './FlushDialog';
import { Icon, IconButton } from './Icon';
import { ObjectList } from './ObjectList';
import { ObjectSettings } from './ObjectSettings';
import { PresetCombo } from './PresetCombo';
import { SettingsTab } from './SettingsTab';
import { filamentColor } from '../plater/Plater3D';

function SectionTitle({ icon, title, children }: { icon: string; title: string; children?: React.ReactNode }) {
  return (
    <div className="side-title">
      <Icon name={icon} size={20} />
      <span className="side-title-text">{title}</span>
      <span className="side-title-tools">{children}</span>
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
  const showBed = machine?.vendor === 'BBL' && !!bedDef?.enum_values;
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
              <select className="combo bed-combo" title="Plate type" value={bedType} onChange={(e) => setState({ bedType: e.target.value })}>
                {bedDef!.enum_values!.map((v, i) =>
                  machine.not_support_bed_type?.includes(v) ? null : (
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
  useStore((s) => s.dirty.filament);
  useStore((s) => s.slotColours);
  const s = getState();
  const add = () =>
    setState((st) => {
      const fils = compatible(st, 'filament');
      const last = st.filamentSlots[st.filamentSlots.length - 1] ?? fils[0]?.name ?? '';
      return { filamentSlots: [...st.filamentSlots, last] };
    });
  const remove = () =>
    setState((st) => ({
      filamentSlots: st.filamentSlots.length > 1 ? st.filamentSlots.slice(0, -1) : st.filamentSlots,
      slotColours: st.slotColours.slice(0, st.filamentSlots.length - 1),
      objects: st.objects.map((o) => (o.extruder >= st.filamentSlots.length ? { ...o, extruder: 1 } : o)),
      events: st.events.filter((e) => e.type !== 'change' || Number(e.text) < st.filamentSlots.length),
    }));
  const flushChanged = useStore((st) => flushModified(st));
  const pickColour = (i: number, colour: string) =>
    setState((st) => {
      const slotColours = [...st.slotColours];
      slotColours[i] = colour.toUpperCase();
      return { slotColours, job: markStale(st.job) };
    });
  return (
    <div className="side-section">
      <SectionTitle icon="filament" title="Project Filaments">
        <IconButton icon="add_filament" title="Add one filament" onClick={add} disabled={slots.length >= 16} />
        <IconButton icon="delete_filament" title="Remove last filament" onClick={remove} disabled={slots.length <= 1} />
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
      </SectionTitle>
      <div className="side-content filament-list">
        {slots.map((_, i) => (
          <div key={i} className="filament-row">
            <label className="filament-swatch" style={{ background: filamentColor(s, i + 1) }} title="Change extruder color">
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
        <span className="adv-label">Advanced</span>
        <button
          type="button"
          className="icon-btn"
          title="Cycle settings visibility"
          onClick={() => setState({ uiMode: uiMode === 'simple' ? 'advanced' : 'simple' })}
        >
          <Icon name={uiMode === 'simple' ? 'toggle_off' : 'toggle_on'} size={28} />
        </button>
      </span>
    </div>
  );
}

export function Sidebar() {
  const hasObjects = useStore((s) => s.objects.length > 0);
  const objects = useStore((s) => s.processScope === 'objects' && s.objects.length > 0);
  return (
    <aside className="sidebar">
      <PrinterSection />
      <FilamentSection />
      <ProcessHeader />
      {hasObjects && <ObjectList />}
      <div className="process-panel">{objects ? <ObjectSettings /> : <SettingsTab type="process" compact />}</div>
    </aside>
  );
}
