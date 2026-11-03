import { Dropdown } from './Dropdown';
import { useState } from 'react';
import { getState, useStore, type CutConnector, type CutConnectorAttribs, type CutGroove } from '../state/store';
import { setCut } from '../plater/cut';
import { CONNECTOR, flipped, GROOVE } from '../plater/cutJoints';
import './CutJoints.css';

const TYPES = [['plug', 'Plug'], ['dowel', 'Dowel'], ['snap', 'Snap']] as const;
const SHAPES = [['triangle', 'Triangle'], ['square', 'Square'], ['hexagon', 'Hexagon'], ['circle', 'Circle']] as const;

function Field({ label, value, onChange, min, max, step = 0.1, disabled, unit = 'mm' }: {
  label: string; value: number; onChange(v: number): void; min: number; max: number; step?: number; disabled?: boolean; unit?: string;
}) {
  return (
    <label className="prefs-row cut-joint-row"><span>{label}</span>
      <input className="text num" type="number" value={value} min={min} max={max} step={step} disabled={disabled}
        onChange={(e) => { const v = Number(e.target.value); if (Number.isFinite(v)) onChange(Math.min(max, Math.max(min, v))); }} />
      <span>{unit}</span>
    </label>
  );
}

function Pair({ label, value, tolerance, onValue, onTolerance, min, max }: {
  label: string; value: number; tolerance: number; onValue(v: number): void; onTolerance(v: number): void; min: number; max: number;
}) {
  const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
  return (
    <label className="prefs-row cut-joint-row"><span>{label}</span>
      <input className="text num" type="number" value={value} min={min} max={max} step={0.1} aria-label={label}
        onChange={(e) => Number.isFinite(+e.target.value) && onValue(clamp(+e.target.value, min, max))} />
      <input className="text num" type="number" value={tolerance} min={0} max={1} step={0.05} aria-label={`${label} tolerance`} title="Tolerance"
        onChange={(e) => Number.isFinite(+e.target.value) && onTolerance(clamp(+e.target.value, 0, 1))} />
      <span>mm</span>
    </label>
  );
}

export default function CutJoints() {
  const cut = useStore((s) => s.cut);
  const [before, setBefore] = useState<CutConnector[] | null>(null);
  if (!cut) return null;
  const mode = cut.mode ?? 'planar';
  const a = { ...CONNECTOR, ...cut.connector };
  const g = { ...GROOVE, ...cut.groove };
  const setA = (p: Partial<CutConnectorAttribs>) => setCut({ connector: { ...a, ...p },
    connectors: cut.connectors?.map((k, i) => (cut.selectedConnectors?.includes(i) ? { ...k, a: { ...k.a, ...p } } : k)) });
  const flip = () => setCut(flipped(getState(), cut));
  const setG = (p: Partial<CutGroove>) => setCut({ groove: { ...g, ...p } });
  const count = cut.connectors?.length ?? 0;

  if (cut.editing)
    return (
      <div className="cut-joints">
        <div className="manip-title">Connectors{count ? ` (${count})` : ''}</div>
        <div className="cut-joint-hint">Left click: Add connector · Right click: Remove connector · Drag: Move connector · Shift+Left click: Add connector to selection · Ctrl+A: Select all connectors</div>
        <div className="prefs-row cut-joint-row"><span>Type</span>
          <span className="cut-joint-radios">
            {TYPES.map(([t, label]) => (
              <label key={t} className="select-row"><input type="radio" name="cut-connector-type" checked={a.type === t}
                onChange={() => setA({ type: t, ...(t === 'dowel' ? { style: 'prism' as const } : t === 'snap' ? { shape: 'circle' as const } : {}) })} />{label}</label>
            ))}
          </span>
        </div>
        <label className="prefs-row cut-joint-row"><span>Style</span>
          <Dropdown className="combo" value={a.type === 'plug' ? a.style : 'prism'} disabled={a.type !== 'plug'} onValueChange={(next) => setA({ style: next as CutConnectorAttribs['style'] })} options={[{ value: "prism", label: 'Prism' }, { value: "frustum", label: 'Frustum' }]} />
        </label>
        <label className="prefs-row cut-joint-row"><span>Shape</span>
          <Dropdown className="combo" value={a.type === 'snap' ? 'circle' : a.shape} disabled={a.type === 'snap'} onValueChange={(next) => setA({ shape: next as CutConnectorAttribs['shape'] })} options={[...SHAPES.map(([v, label]) => ({ value: v, label }))]} />
        </label>
        <Pair label="Depth" value={a.depth} tolerance={a.depthTolerance} min={a.type === 'snap' ? a.size : 0.1} max={100}
          onValue={(depth) => setA({ depth })} onTolerance={(depthTolerance) => setA({ depthTolerance })} />
        <Pair label="Size" value={a.size} tolerance={a.sizeTolerance} min={0.5} max={100}
          onValue={(size) => setA({ size, ...(a.type === 'snap' && a.depth < size ? { depth: size } : {}) })} onTolerance={(sizeTolerance) => setA({ sizeTolerance })} />
        <Field label="Rotation" value={a.angle} min={0} max={180} step={1} unit="°" onChange={(angle) => setA({ angle })} />
        {a.type === 'snap' && <>
          <Field label="Bulge" value={Math.round(a.bulge * 100)} min={5} max={Math.round(a.space * 100)} step={1} unit="%" onChange={(v) => setA({ bulge: v / 100 })} />
          <Field label="Space" value={Math.round(a.space * 100)} min={10} max={50} step={1} unit="%" onChange={(v) => setA({ space: v / 100, bulge: Math.min(a.bulge, v / 100) })} />
        </>}
        <div className="cut-joint-buttons">
          <button className="btn" disabled={!count} onClick={() => setCut({ connectors: [], selectedConnectors: [] })}>Remove connectors</button>
          <button className="btn" disabled={count > 0} title="Flip cut plane" onClick={flip}>Flip cut plane</button>
          <span style={{ flex: 1 }} />
          <button className="btn primary" onClick={() => { setBefore(null); setCut({ editing: false, selectedConnectors: [] }); }}>Confirm connectors</button>
          <button className="btn" onClick={() => { setCut({ editing: false, selectedConnectors: [], connectors: before ?? cut.connectors }); setBefore(null); }}>Cancel</button>
        </div>
      </div>
    );

  const both = cut.keepUpper && cut.keepLower && !cut.toParts;
  return (
    <div className="cut-joints">
      <label className="prefs-row cut-joint-row"><span>Mode</span>
        <Dropdown className="combo" value={mode} onValueChange={(next) => setCut({ mode: next as 'planar' | 'dovetail', ...(next === 'dovetail' ? { keepUpper: true, keepLower: true, toParts: false } : {}) })} options={[{ value: "planar", label: 'Planar' }, { value: "dovetail", label: 'Dovetail' }]} />
      </label>
      {mode === 'planar' ? (
        <button className="btn" disabled={!both} title={both ? undefined : 'Connectors need both parts kept as separate objects'}
          onClick={() => { setBefore(cut.connectors ?? []); setCut({ editing: true }); }}>
          {count ? `Edit connectors (${count})` : 'Add connectors'}
        </button>
      ) : (
        <>
          <div className="cut-joint-section">Groove:</div>
          <Pair label="Depth" value={g.depth} tolerance={g.depthTolerance} min={0.1} max={100} onValue={(depth) => setG({ depth })} onTolerance={(depthTolerance) => setG({ depthTolerance })} />
          <Pair label="Width" value={g.width} tolerance={g.widthTolerance} min={0.1} max={200} onValue={(width) => setG({ width })} onTolerance={(widthTolerance) => setG({ widthTolerance })} />
          <Field label="Flap Angle" value={g.flapsAngle} min={30} max={120} step={1} unit="°" onChange={(flapsAngle) => setG({ flapsAngle })} />
          <Field label="Groove Angle" value={g.angle} min={0} max={15} step={1} unit="°" onChange={(angle) => setG({ angle })} />
          <div className="cut-joint-section">Multiple:</div>
          <Field label="Count" value={g.count} min={1} max={100} step={1} unit="" onChange={(count) => setG({ count: Math.round(count) })} />
          <Field label="Gap" value={g.gap} min={0} max={100} disabled={g.count === 1} onChange={(gap) => setG({ gap })} />
        </>
      )}
    </div>
  );
}
