import { Dropdown, EditableDropdown } from './Dropdown';
import { useMemo, useState } from 'react';
import type { Config } from '../api/types';
import { effectiveConfig, getState, printerPreset, resolvePreset, toast, useStore, type State } from '../state/store';
import { saveProfile } from '../profiles/model';
import { commitProfileRecords } from '../profiles/transactions';
import { profileKey } from '../profiles/identity';
import { switchPreset } from '../actions';
import { Modal, ModalBody, ModalFooter, ModalTitle } from './Modal';

const NOZZLES = ['0.2', '0.4', '0.6', '0.8'];
const first = (v: unknown) => String([v].flat()[0] ?? '');
function rect(area: string) {
  const pts = area.split(',').map((p) => p.split('x').map(Number));
  const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  return { w: Math.max(...xs) - Math.min(...xs), d: Math.max(...ys) - Math.min(...ys), ox: -Math.min(...xs), oy: -Math.min(...ys) };
}

export default function CreatePrinter({ close }: { close(): void }) {
  const s: State = getState();
  const ready = useStore((st) => st.profileLibraryLoad.state === 'ready');
  const machines = useMemo(() => [...s.machines].sort((a, b) => (a.name < b.name ? -1 : 1)), [s.machines]);
  const vendors = useMemo(() => [...new Set(machines.map((m) => m.vendor))].sort(), [machines]);
  const current = printerPreset(s);
  const [mode, setMode] = useState<'printer' | 'nozzle'>('printer');
  const [fromCurrent, setFromCurrent] = useState(false);
  const [templateVendor, setTemplateVendor] = useState(current?.vendor ?? vendors[0]);
  const [template, setTemplate] = useState(current?.name ?? machines[0]?.name);
  const onCurrent = fromCurrent || mode === 'nozzle';
  const base = machines.find((m) => m.name === (onCurrent ? current?.name : template)) ?? current;
  const shape = onCurrent ? effectiveConfig(s, 'machine') : base?.config ?? {};
  const r = rect(String(shape.printable_area ?? '0x0,200x0,200x200,0x200'));
  const [vendor, setVendor] = useState('');
  const [model, setModel] = useState('');
  const [nozzle, setNozzle] = useState(first(current?.config.nozzle_diameter) || '0.4');
  const [size, setSize] = useState<[number, number] | null>(null);
  const [origin, setOrigin] = useState<[number, number] | null>(null);
  const [height, setHeight] = useState<number | null>(null);
  const [w, d] = size ?? [r.w, r.d], [ox, oy] = origin ?? [r.ox, r.oy];
  const h = height ?? (Number(shape.printable_height) || 250);
  const name = mode === 'nozzle' ? resolvePreset(s,'machine',s.printer).name ?? 'Printer' : `${vendor.trim()} ${model.trim()} ${nozzle} nozzle`;
  const missing = mode === 'printer' && (!vendor.trim() || !model.trim()) ? 'Enter a vendor and a model.'
    : !(Number(nozzle) > 0) ? 'Enter the nozzle diameter.'
    : mode === 'printer' && (s.machines.some((m) => m.name === name) || s.userPresets.some((u) => u.type === 'machine' && u.name === name)) ? `“${name}” already exists.` : '';
  const create = async () => {
    if (!base || missing) return;
    if (getState().profileLibraryLoad.state !== 'ready') return;
    const nd = base.config.nozzle_diameter;
    const overrides: Config = {
      printable_area: `${-ox}x${-oy},${w - ox}x${-oy},${w - ox}x${d - oy},${-ox}x${d - oy}`,
      printable_height: String(h),
      nozzle_diameter: Array.isArray(nd) ? (nd as unknown[]).map(() => nozzle) : nozzle,
    };
    try {
      const profile=saveProfile(s,'machine',name,mode === 'nozzle' ? s.printer : base.name,{...shape,...overrides});
      await commitProfileRecords([profile]);
      if(mode === 'printer')switchPreset('machine',0,profileKey(profile));
      toast(`Created printer ${name}`);
      close();
    } catch (e) {
      toast(`Create printer failed: ${e instanceof Error ? e.message : String(e)}`);
    }
  };
  const num = (label: string, v: number, set: (n: number) => void, min = -1000) => <label className="form-row"><span>{label}</span>
    <span className="modal-unit"><input className="text num" type="number" step={1} min={min} value={v} onChange={(e) => { const n = Number(e.target.value); if (Number.isFinite(n)) set(n); }} /><span>mm</span></span></label>;
  const seg = <T extends string>(label: string, value: T, set: (v: T) => void, options: [T, string][]) => <div className="form-row"><span>{label}</span>
    <span className="seg" role="radiogroup" aria-label={label}>{options.map(([v, text]) => <button key={v} type="button" role="radio" aria-checked={value === v} onClick={() => set(v)}>{text}</button>)}</span></div>;
  return (
    <Modal size="m" label="Create printer" onClose={close}>
      <ModalTitle title="Create printer" onClose={close} />
      <ModalBody className="modal-fields">
        {seg('Create', mode, setMode, [['printer', 'Printer'], ['nozzle', 'Nozzle']])}
        {mode === 'printer' ? <>
          <label className="form-row"><span>Vendor</span><input className="text" value={vendor} onChange={(e) => setVendor(e.target.value)} /></label>
          <label className="form-row"><span>Model</span><input className="text" value={model} onChange={(e) => setModel(e.target.value)} /></label>
          {seg('Start from', fromCurrent ? 'current' : 'template', (v) => setFromCurrent(v === 'current'), [['template', 'Template'], ['current', 'Current']])}
          {!fromCurrent && <>
            <label className="form-row"><span>Template vendor</span>
              <Dropdown className="combo" value={templateVendor} aria-label="Template vendor" onValueChange={(next) => { setTemplateVendor(next); setTemplate(machines.find((m) => m.vendor === next)?.name ?? ''); setSize(null); setOrigin(null); setHeight(null); }} options={[...vendors.map((v) => ({ value: v, label: v }))]} />
            </label>
            <label className="form-row"><span>Template</span>
              <Dropdown className="combo" value={template} aria-label="Template printer" onValueChange={(next) => { setTemplate(next); setSize(null); setOrigin(null); setHeight(null); }} options={[...machines.filter((m) => m.vendor === templateVendor).map((m) => ({ value: m.name, label: m.name }))]} />
            </label>
          </>}
        </> : <div className="form-row"><span>Printer</span><span>{resolvePreset(s,'machine',s.printer).name ?? 'Unavailable printer'}</span></div>}
        <label className="form-row"><span>Nozzle</span>
          <span className="modal-unit"><EditableDropdown className="text num" value={nozzle} aria-label="Nozzle diameter" onValueChange={setNozzle} onInputValueChange={setNozzle} options={NOZZLES.map(n => ({value: n, label: n}))} /><span>mm</span></span>
        </label>
        <h3 className="modal-group">Bed</h3>
        {num('Width', w, (n) => setSize([n, d]), 1)}{num('Depth', d, (n) => setSize([w, n]), 1)}
        {num('Origin X', ox, (n) => setOrigin([n, oy]))}{num('Origin Y', oy, (n) => setOrigin([ox, n]))}
        {num('Max height', h, setHeight, 1)}
        {missing && (vendor.trim() || model.trim() || mode === 'nozzle') && <p className="modal-error" role="alert">{missing}</p>}
      </ModalBody>
      <ModalFooter lead={!missing && <span data-full-text={name}>{name}</span>}>
        <button type="button" className="btn" onClick={close}>Cancel</button>
        <button type="button" className="btn-primary" disabled={!!missing || !ready || !s.api || s.api.mock} onClick={() => void create()}>Create</button>
      </ModalFooter>
    </Modal>
  );
}
