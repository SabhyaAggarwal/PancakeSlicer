import { Dropdown } from './Dropdown';
import { useState } from 'react';
import { OPTIONS } from '../options';
import { getState, printerPreset, useStore } from '../state/store';
import { filamentColor } from '../plater/filamentColor';
import { setPlateSettings } from '../plater/plates';
import { Modal, ModalTitle } from './Modal';

type Range = { from: number; to: number; order: number[] };
const ints = (v: unknown) => String(v ?? '').split(/[,;]/).map((x) => Number(x.trim())).filter((x) => Number.isFinite(x));

function ranges(v: unknown, nums: unknown, n: number): Range[] {
  const flat = ints(v), count = Number(nums) || 0, out: Range[] = [];
  for (let k = 0, i = 0; k < count && i + 2 + n <= flat.length; k++, i += 2 + n) out.push({ from: flat[i], to: flat[i + 1], order: flat.slice(i + 2, i + 2 + n) });
  return out;
}

function Order({ order, colours, onChange }: { order: number[]; colours: string[]; onChange(o: number[]): void }) {
  const move = (i: number, d: number) => { const o = [...order]; [o[i], o[i + d]] = [o[i + d], o[i]]; onChange(o); };
  return (
    <span className="plate-order">
      {order.map((f, i) => (
        <span key={f} className="plate-order-item">
          <span className="menu-swatch" style={{ background: colours[f - 1] }} />{f}
          <button type="button" className="btn small" disabled={i === 0} aria-label="Move up" onClick={() => move(i, -1)}>‹</button>
          <button type="button" className="btn small" disabled={i === order.length - 1} aria-label="Move down" onClick={() => move(i, 1)}>›</button>
        </span>
      ))}
    </span>
  );
}

export default function PlateSettings({ close }: { close(): void }) {
  const plate = useStore((s) => s.projectPlates.find((p) => p.index === s.plateIndex));
  const s = getState();
  const n = s.filamentSlots.length, colours = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
  const natural = Array.from({ length: n }, (_, i) => i + 1);
  const cfg = plate?.settings ?? {};
  const [name, setName] = useState(plate?.name ?? '');
  const [bed, setBed] = useState(String(cfg.curr_bed_type ?? ''));
  const [seq, setSeq] = useState(String(cfg.print_sequence ?? ''));
  const [spiral, setSpiral] = useState(cfg.spiral_mode === undefined ? '' : ['1', 'true'].includes(String(cfg.spiral_mode)) ? '1' : '0');
  const first0 = ints(cfg.first_layer_print_sequence).filter((f) => f >= 1 && f <= n);
  const [first, setFirst] = useState(first0.length === n ? first0 : null);
  const [others, setOthers] = useState<Range[] | null>(() => { const r = ranges(cfg.other_layers_print_sequence, cfg.other_layers_print_sequence_nums, n); return r.length ? r : null; });
  const bedDef = OPTIONS.curr_bed_type, seqDef = OPTIONS.print_sequence;
  const machine = printerPreset(s), multiBed = machine?.config.support_multi_bed_types;
  const bedTypes = machine?.vendor === 'BBL' || multiBed === true || multiBed === 1 || multiBed === '1';
  const beds = (bedDef?.enum_values ?? []).map((v, i) => [v, bedDef?.enum_labels?.[i] ?? v] as const)
    .filter(([v, label]) => !machine?.not_support_bed_type?.some((b) => b === v || b === label));
  const ok = () => {
    const out: Record<string, string> = {};
    if (bed) out.curr_bed_type = bed;
    if (seq) out.print_sequence = seq;
    if (spiral) out.spiral_mode = spiral;
    if (first) out.first_layer_print_sequence = first.join(',');
    if (others?.length) {
      out.other_layers_print_sequence = others.flatMap((r) => [r.from, r.to, ...r.order]).join(',');
      out.other_layers_print_sequence_nums = String(others.length);
    }
    setPlateSettings(name, out);
    close();
  };
  return (
    <Modal size="m" label="Plate Settings" onClose={close}>
      <ModalTitle title="Plate Settings" onClose={close} />
      <div className="prefs-body plate-settings">
        <label className="prefs-row"><span>Plate name</span><input className="text" value={name} onChange={(e) => setName(e.target.value)} /></label>
        <label className="prefs-row"><span>Bed type</span>
          <Dropdown className="combo" value={bed} disabled={!bedTypes} onValueChange={setBed} options={[{ value: "", label: 'Same as Global Plate Type' }, ...beds.map(([v, label]) => ({ value: v, label }))]} />
        </label>
        <label className="prefs-row"><span>Print sequence</span>
          <Dropdown className="combo" value={seq} onValueChange={setSeq} options={[{ value: "", label: 'Same as Global Print Sequence' }, ...(seqDef?.enum_values?.map((v, i) => ({ value: v, label: seqDef.enum_labels?.[i] ?? v })) ?? [])]} />
        </label>
        <label className="prefs-row"><span>Spiral vase</span>
          <Dropdown className="combo" value={spiral} onValueChange={setSpiral} options={[{ value: "", label: 'Same as Global' }, { value: "1", label: 'Enable' }, { value: "0", label: 'Disable' }]} />
        </label>
        {n > 1 && <>
          <label className="prefs-row"><span>First layer filament sequence</span>
            <Dropdown className="combo" value={first ? 'c' : 'a'} onValueChange={(next) => setFirst(next === 'c' ? natural : null)} options={[{ value: "a", label: 'Auto' }, { value: "c", label: 'Customize' }]} />
          </label>
          {first && <Order order={first} colours={colours} onChange={setFirst} />}
          <label className="prefs-row"><span>Other layer filament sequence</span>
            <Dropdown className="combo" value={others ? 'c' : 'a'} onValueChange={(next) => setOthers(next === 'c' ? [{ from: 2, to: 2, order: natural }] : null)} options={[{ value: "a", label: 'Auto' }, { value: "c", label: 'Customize' }]} />
          </label>
          {others?.map((r, k) => (
            <div key={k} className="prefs-row plate-range">
              <span>Layer</span>
              <input className="text num" type="number" min={2} value={r.from} onChange={(e) => setOthers(others.map((x, j) => (j === k ? { ...x, from: Math.max(2, Number(e.target.value)) } : x)))} />
              <span>to</span>
              <input className="text num" type="number" min={r.from} value={r.to} onChange={(e) => setOthers(others.map((x, j) => (j === k ? { ...x, to: Math.max(x.from, Number(e.target.value)) } : x)))} />
              <Order order={r.order} colours={colours} onChange={(o) => setOthers(others.map((x, j) => (j === k ? { ...x, order: o } : x)))} />
              <button type="button" className="btn small" aria-label="Delete" onClick={() => setOthers(others.length > 1 ? others.filter((_, j) => j !== k) : null)}>−</button>
            </div>
          ))}
          {others && <button type="button" className="btn small" onClick={() => { const last = others[others.length - 1]; setOthers([...others, { from: last.to + 1, to: last.to + 1, order: natural }]); }}>+</button>}
        </>}
      </div>
      <div className="modal-buttons">
        <button className="btn primary" data-autofocus onClick={ok}>OK</button>
        <button className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}
