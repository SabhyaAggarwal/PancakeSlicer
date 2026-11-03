import { lazy, Suspense, useState } from 'react';
import { OPTIONS } from '../options';
import { compatible, filamentSwitchReason, getState, markStale, printerPreset, selectionOf, setState, toast, useStore, type State } from '../state/store';
import { undo, undoable } from '../state/history';
import { flushModified } from '../flush-state';
import { IconButton } from '../components/Icon';
import { Dropdown } from '../components/Dropdown';
import { requestPreset } from '../components/PresetCombo';
import { AddFilamentPicker, FilamentPicker } from './Pickers';
import { filamentColor } from '../plater/filamentColor';
import './profiles.css';
import { filamentSlots, modelKey, modelPreset, nozzleOf, printerModels } from './machines';

const MixedFilaments = lazy(() => import('../components/MixedFilaments'));

export function NozzleField() {
  const machine = useStore((s) => printerPreset(s));
  useStore((s) => s.machines);
  useStore((s) => s.userPresets);
  const printer = useStore((s) => s.printer);
  const variants = machine ? printerModels(getState()).get(modelKey(machine)) ?? [] : [];
  const sizes = [...new Set(variants.map(nozzleOf).filter((d) => Number.isFinite(d) && d > 0))].sort((a, b) => a - b);
  if (sizes.length < 2 || !machine) return null;
  return (
    <div className="opt-line">
      <span className="opt-label">Nozzle</span>
      <span className="opt-field">
        <Dropdown className="combo" aria-label="Nozzle" value={String(nozzleOf(machine))}
          options={sizes.map((d) => ({ value: String(d), label: `${d} mm` }))}
          onValueChange={(v) => {
            const next = modelPreset(variants.filter((p) => nozzleOf(p) === Number(v)), printer, Number(v));
            if (next.name !== printer) requestPreset('machine', 0, next.name);
          }} />
      </span>
    </div>
  );
}

export const NEW_COLOUR = () => NEXT_COLOURS[nextColour++ % NEXT_COLOURS.length];

export function addFilament() {
  const s = getState();
  if (Object.keys(s.mixes).length) return void import('../plater/mixed').then((m) => m.addPhysical(NEW_COLOUR()));
  undoable('Add filament', () => setState((st) => {
    const fils = compatible(st, 'filament');
    const last = st.filamentSlots[st.filamentSlots.length - 1] ?? fils[0]?.name ?? '';
    const slotColours = [...st.slotColours];
    slotColours[st.filamentSlots.length] = NEW_COLOUR();
    return { filamentSlots: [...st.filamentSlots, last], slotColours };
  }));
}

export function addSlot(name: string, colour?: string) {
  undoable('Add filament', () => setState((st) => {
    const slotColours = [...st.slotColours];
    slotColours[st.filamentSlots.length] = colour && /^#[0-9a-f]{6}$/i.test(colour) ? colour.toUpperCase() : NEW_COLOUR();
    return { filamentSlots: [...st.filamentSlots, name], slotColours, job: markStale(st.job) };
  }));
}

export function removeFilament() {
  const s = getState();
  if (Object.keys(s.mixes).length) return void import('../plater/mixed').then((m) => m.removePhysical());
  undoable('Remove filament', () => setState((st) => ({
    filamentSlots: st.filamentSlots.length > 1 ? st.filamentSlots.slice(0, -1) : st.filamentSlots,
    slotColours: st.slotColours.slice(0, st.filamentSlots.length - 1),
    objects: st.objects.map((o) => (o.extruder >= st.filamentSlots.length ? { ...o, extruder: 1 } : o)),
    events: st.events.filter((e) => e.type !== 'change' || Number(e.text) < st.filamentSlots.length),
  })));
}

export const pickColour = (i: number, colour: string) =>
  undoable('Change filament colour', () => setState((st) => {
    const slotColours = [...st.slotColours];
    slotColours[i] = colour.toUpperCase();
    return { slotColours, job: markStale(st.job) };
  }), `colour:${i}`);

export function removeSlot(i: number) {
  const s = getState();
  const painted = s.objects.some((o) => o.paint?.color || o.volumes?.some((v) => v.paint?.color));
  if (Object.keys(s.mixes).length || painted || i === s.filamentSlots.length - 1) return removeFilament();
  const n = i + 1;
  const remap = (e: number) => (e === n ? 1 : e > n ? e - 1 : e);
  undoable('Remove filament', () => setState((st) => ({
    filamentSlots: st.filamentSlots.filter((_, j) => j !== i),
    slotColours: st.slotColours.filter((_, j) => j !== i),
    objects: st.objects.map((o) => ({ ...o, extruder: remap(o.extruder), ...(o.volumes ? { volumes: o.volumes.map((v) => (v.extruder ? { ...v, extruder: remap(v.extruder) } : v)) } : {}) })),
    events: st.events.filter((e) => e.type !== 'change' || Number(e.text) !== n)
      .map((e) => (e.type === 'change' && Number(e.text) > n ? { ...e, text: String(Number(e.text) - 1) } : e)),
    flush: { ...st.flush, matrix: null },
    projectPresets: st.projectPresets && { ...st.projectPresets, filaments: st.projectPresets.filaments.filter((_, j) => j !== i) },
    job: markStale(st.job),
  })));
  toast('Removed', { label: 'Undo', run: undo });
}

const lastSendPrinter = () => { try { return localStorage.getItem('orca.web.sendPrinter'); } catch { return null; } };

const usersOf = (s: State, n: number) => s.objects.filter((o) => o.extruder === n || o.volumes?.some((v) => v.extruder === n));

function selectUsers(n: number) {
  const list = usersOf(getState(), n);
  if (!list.length) return;
  setState({ selected: selectionOf(getState(), list.flatMap((o) => o.instances.map((inst) => `${o.id}:${inst.id}`))) });
}

export function MaterialRows() {
  const slots = useStore((s) => s.filamentSlots);
  const physical = slots.length - Object.keys(useStore((s) => s.mixes)).length;
  useStore((s) => s.filamentEdits);
  useStore((s) => s.slotColours);
  useStore((s) => s.projectMaterials);
  useStore((s) => s.objects);
  const s = getState();
  return (
    <table className="pf-table pf-filaments">
      <colgroup><col className="c-n" /><col /><col className="c-obj" /><col className="c-act" /></colgroup>
      <thead><tr><th className="n">#</th><th>Filament</th><th className="n" title="Objects">Obj</th><th aria-label="Actions" /></tr></thead>
      <tbody>
        {slots.slice(0, physical).map((_, i) => {
          const used = usersOf(s, i + 1).length;
          return (
            <tr key={i}>
              <td className="n">{i + 1}</td>
              <td><FilamentPicker slot={i} onColour={(c) => pickColour(i, c)} />{(() => { const r = filamentSwitchReason(s, i); return r && <div className="pf-dim pf-reason">{r}</div>; })()}</td>
              <td className="n">{used ? <button type="button" className="pf-link" onClick={() => selectUsers(i + 1)}>{used}</button> : <span className="pf-dim">0</span>}</td>
              <td className="pf-acts">
                <IconButton icon="edit" title="Edit" onClick={() => setState({ dialog: { type: 'filament', slot: i } })} />
                {physical > 1 && <IconButton icon="delete" title="Remove" onClick={() => removeSlot(i)} />}
              </td>
            </tr>
          );
        })}
        <tr className="pf-add-row">
          <td colSpan={4}>{Object.keys(getState().mixes).length
            ? <button type="button" className="pf-link" onClick={addFilament} disabled={slots.length >= 64}>Add</button>
            : <AddFilamentPicker disabled={slots.length >= 64} />}</td>
        </tr>
      </tbody>
    </table>
  );
}


export function PlateRow() {
  return <><NozzleField /><PlateType /></>;
}

function PlateType() {
  const machine = useStore((s) => printerPreset(s));
  const bedType = useStore((s) => s.bedType);
  const bedDef = OPTIONS.curr_bed_type;
  const multiBed = machine?.config.support_multi_bed_types;
  if (!(machine?.vendor === 'BBL' || multiBed === true || multiBed === 1 || multiBed === '1') || !bedDef?.enum_values) return null;
  return (
    <div className="opt-line">
      <span className="opt-label">Plate type</span>
      <Dropdown className="combo bed-combo" aria-label="Plate type" value={bedType}
        onValueChange={(value) => undoable('Change plate type', () => setState({ bedType: value }))}
        options={bedDef.enum_values.flatMap((v, i) =>
          machine?.not_support_bed_type?.some((b) => b === v || b === bedDef.enum_labels?.[i])
            ? [] : [{ value: v, label: bedDef.enum_labels?.[i] ?? v }])} />
    </div>
  );
}

export function SlotStrip({ slot, pick }: { slot: number; pick(i: number): void }) {
  const slots = useStore((s) => s.filamentSlots);
  useStore((s) => s.slotColours);
  const physical = slots.length - Object.keys(useStore((s) => s.mixes)).length;
  const capacity = useStore((st) => filamentSlots(printerPreset(st)));
  useStore((st) => st.objects);
  const used = usersOf(getState(), slot + 1).length;
  return (
    <div className="rail-slots" role="tablist" aria-label="Filament slots">
      {slots.slice(0, physical).map((_, i) => (
        <button key={i} type="button" role="tab" aria-selected={i === slot} className="rail-slot" title={`Filament ${i + 1}`} onClick={() => pick(i)}>
          <span className="pk-swatch" style={{ background: filamentColor(getState(), i + 1) }} />{i + 1}
        </button>
      ))}
      <AddFilamentPicker disabled={slots.length >= Math.max(capacity, 1)} />
      {used > 0 && <button type="button" className="pf-link rail-uses" title="Select the objects printed with this slot" onClick={() => selectUsers(slot + 1)}>{used} {used === 1 ? 'object' : 'objects'}</button>}
      {physical > 1 && <IconButton icon="delete" title="Remove filament" onClick={() => removeSlot(slot)} />}
    </div>
  );
}

export function MaterialTools() {
  const slots = useStore((s) => s.filamentSlots.length);
  const physical = slots - Object.keys(useStore((s) => s.mixes)).length;
  const flushChanged = useStore((st) => flushModified(st));
  const amsPrinter = useStore((st) => {
    const bambu = st.printers.filter((p) => p.kind === 'bambu-lan');
    return (bambu.find((p) => p.name === lastSendPrinter()) ?? bambu[0])?.name;
  });
  const [syncing, setSyncing] = useState(false);
  const sync = () => {
    if (!amsPrinter) return;
    setSyncing(true);
    void import('../plater/amsSync').then((m) => m.syncAms(amsPrinter)).finally(() => setSyncing(false));
  };
  if (slots < 2 && !amsPrinter) return null;
  return (
    <>
      <div className="setup-row setup-tools">
        {slots > 1 && <button type="button" className={`btn pf-flush ${flushChanged ? 'modified' : ''}`} onClick={() => setState({ flushDialog: true })}>Flushing</button>}
        {amsPrinter && <IconButton icon="ams_fila_sync" title="Sync AMS" onClick={sync} disabled={syncing} />}
      </div>
      {physical > 1 && <Suspense fallback={null}><MixedFilaments /></Suspense>}
    </>
  );
}


const NEXT_COLOURS = ['#00C1AE', '#F4E2C1', '#ED1C24', '#00FF7F', '#F26722', '#FFEB31', '#7841CE', '#115877',
  '#ED1E79', '#2EBDEF', '#345B2F', '#800080', '#FA8173', '#800000', '#F7B763', '#A4C41E'];
let nextColour = 0;

export function FilamentSlotsView({ slot, pick }: { slot: number; pick(i: number): void }) {
  const multi = useStore((st) => filamentSlots(printerPreset(st)) > 1 || st.filamentSlots.length > 1);
  if (!multi) return null;
  return <><SlotStrip slot={slot} pick={pick} /><MaterialTools /></>;
}
