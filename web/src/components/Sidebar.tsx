import { OPTIONS } from '../options';
import { compatible, getState, printerPreset, setState, useStore } from '../state/store';
import { Icon, IconButton } from './Icon';
import { ObjectList } from './ObjectList';
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
      objects: st.objects.map((o) => (o.extruder >= st.filamentSlots.length ? { ...o, extruder: 1 } : o)),
    }));
  return (
    <div className="side-section">
      <SectionTitle icon="filament" title="Project Filaments">
        <IconButton icon="add_filament" title="Add one filament" onClick={add} disabled={slots.length >= 16} />
        <IconButton icon="delete_filament" title="Remove last filament" onClick={remove} disabled={slots.length <= 1} />
      </SectionTitle>
      <div className="side-content filament-list">
        {slots.map((_, i) => (
          <div key={i} className="filament-row">
            <span className="filament-swatch" style={{ background: filamentColor(s, i + 1) }}>
              {i + 1}
            </span>
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
  return (
    <div className="side-title process-title">
      <Icon name="process" size={20} />
      <span className="side-title-text">Process</span>
      <span className="mode-switch" title="Per-object settings arrive with the object settings panel">
        <button className="active">Global</button>
        <button disabled>Objects</button>
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
  return (
    <aside className="sidebar">
      <PrinterSection />
      <FilamentSection />
      <ProcessHeader />
      {hasObjects && <ObjectList />}
      <div className="process-panel">
        <SettingsTab type="process" compact />
      </div>
    </aside>
  );
}
