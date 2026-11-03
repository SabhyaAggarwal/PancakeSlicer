import { Dropdown, EditableDropdown } from './Dropdown';
import { useMemo, useState } from 'react';
import type { Config } from '../api/types';
import { effectiveConfig, getState, printerPreset, resolvePreset, toast, type State } from '../state/store';
import { saveProfile } from '../profiles/model';
import { commitProfileRecords } from '../profiles/transactions';
import { profileKey } from '../profiles/identity';
import { switchPreset } from '../actions';
import { Modal, ModalTitle } from './Modal';

const NOZZLES = ['0.2', '0.4', '0.6', '0.8'];
const first = (v: unknown) => String([v].flat()[0] ?? '');
function rect(area: string) {
  const pts = area.split(',').map((p) => p.split('x').map(Number));
  const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  return { w: Math.max(...xs) - Math.min(...xs), d: Math.max(...ys) - Math.min(...ys), ox: -Math.min(...xs), oy: -Math.min(...ys) };
}

export default function CreatePrinter({ close }: { close(): void }) {
  const s: State = getState();
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
  const missing = mode === 'printer' && (!vendor.trim() || !model.trim()) ? 'Input a vendor and a model'
    : !(Number(nozzle) > 0) ? 'Input the nozzle diameter'
    : mode === 'printer' && (s.machines.some((m) => m.name === name) || s.userPresets.some((u) => u.type === 'machine' && u.name === name)) ? `A printer named "${name}" already exists` : '';
  const create = async () => {
    if (!base || missing) return;
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
  const num = (label: string, v: number, set: (n: number) => void, min = -1000) => (
    <input className="text num" type="number" step={1} min={min} value={v} aria-label={label} onChange={(e) => { const n = Number(e.target.value); if (Number.isFinite(n)) set(n); }} />
  );
  return (
    <Modal label="Create Printer/Nozzle" onClose={close} className="small create-printer">
      <ModalTitle title="Create Printer/Nozzle" onClose={close} />
      <div className="prefs-body">
        <div className="prefs-row"><span>Create Type</span>
          <span className="cut-joint-buttons">
            <label className="select-row"><input type="radio" checked={mode === 'printer'} onChange={() => setMode('printer')} />Create Printer</label>
            <label className="select-row"><input type="radio" checked={mode === 'nozzle'} onChange={() => setMode('nozzle')} />Create Nozzle for Existing Printer</label>
          </span>
        </div>
        {mode === 'printer' ? <>
          <label className="prefs-row"><span>Vendor</span><input className="text" value={vendor} placeholder="Input Custom Vendor" aria-label="Vendor" onChange={(e) => setVendor(e.target.value)} /></label>
          <label className="prefs-row"><span>Model</span><input className="text" value={model} placeholder="Input Custom Model" aria-label="Model" onChange={(e) => setModel(e.target.value)} /></label>
          <div className="prefs-row"><span>Base</span>
            <span className="cut-joint-buttons">
              <label className="select-row"><input type="radio" checked={!fromCurrent} onChange={() => setFromCurrent(false)} />Create from Template</label>
              <label className="select-row"><input type="radio" checked={fromCurrent} onChange={() => setFromCurrent(true)} />Create Based on Current Printer</label>
            </span>
          </div>
          {!fromCurrent && <>
            <label className="prefs-row"><span>Template vendor</span>
              <Dropdown className="combo" value={templateVendor} aria-label="Template vendor" onValueChange={(next) => { setTemplateVendor(next); setTemplate(machines.find((m) => m.vendor === next)?.name ?? ''); setSize(null); setOrigin(null); setHeight(null); }} options={[...vendors.map((v) => ({ value: v, label: v }))]} />
            </label>
            <label className="prefs-row"><span>Template printer</span>
              <Dropdown className="combo" value={template} aria-label="Template printer" onValueChange={(next) => { setTemplate(next); setSize(null); setOrigin(null); setHeight(null); }} options={[...machines.filter((m) => m.vendor === templateVendor).map((m) => ({ value: m.name, label: m.name }))]} />
            </label>
          </>}
        </> : <div className="prefs-row"><span>Printer</span><span>{resolvePreset(s,'machine',s.printer).name ?? 'Unavailable printer'}</span></div>}
        <label className="prefs-row"><span>Nozzle Diameter</span>
          <EditableDropdown className="text num" value={nozzle} aria-label="Nozzle Diameter" onValueChange={setNozzle} onInputValueChange={setNozzle} options={NOZZLES.map(n => ({value: n, label: n}))} /><span>mm</span>
        </label>
        <div className="cut-joint-section">Printable Space</div>
        <div className="prefs-row"><span>Bed Shape</span><span>Rectangle</span></div>
        <div className="prefs-row"><span>Size</span>{num('Size X', w, (n) => setSize([n, d]), 1)}{num('Size Y', d, (n) => setSize([w, n]), 1)}<span>mm</span></div>
        <div className="prefs-row"><span>Origin</span>{num('Origin X', ox, (n) => setOrigin([n, oy]))}{num('Origin Y', oy, (n) => setOrigin([ox, n]))}<span>mm</span></div>
        <div className="prefs-row"><span>Max Print Height</span>{num('Max Print Height', h, setHeight, 1)}<span>mm</span></div>
        <div className="prefs-row"><span>Name</span><span title={name}>{name}</span></div>
      </div>
      <div className="modal-buttons">
        {missing && <span className="brim-warning">{missing}</span>}
        <span style={{ flex: 1 }} />
        <button type="button" className="btn" onClick={close}>Cancel</button>
        <button type="button" className="btn primary" disabled={!!missing || !s.api || s.api.mock} onClick={() => void create()}>Create</button>
      </div>
    </Modal>
  );
}
