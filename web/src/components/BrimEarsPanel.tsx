import { Range } from './Range';
import { useEffect, useSyncExternalStore } from 'react';
import { getState, setState, updateObject, useStore } from '../state/store';
import { autoGenerate, brimPainted, defaultEarRadius, earsUi, setEars, setEarsUi, subscribeEarsUi } from '../plater/brimEars';

export default function BrimEarsPanel() {
  const ui = useSyncExternalStore(subscribeEarsUi, earsUi);
  const o = useStore((s) => s.objects.find((x) => x.id === s.selected?.object));
  const painted = useStore((s) => !!o && brimPainted(s, o));
  useEffect(() => { if (!earsUi().radius) setEarsUi({ radius: +defaultEarRadius().toFixed(2) }); }, []);
  if (!o) return null;
  const ears = o.brimEars ?? [];
  const row = (label: string, value: number, min: number, max: number, step: number, set: (v: number) => void) => (
    <label className="prefs-row brim-row"><span>{label}</span>
      <Range min={min} max={max} step={step} value={value} aria-label={label} onValueChange={(next) => set(next)} />
      <input className="text num" type="number" min={min} max={max} step={step} value={value} onChange={(e) => set(Math.min(max, Math.max(min, Number(e.target.value) || min)))} />
    </label>
  );
  return (
    <div className="manip-panel brim-ears">
      <div className="manip-title">Brim Ears</div>
      <div className="cut-joint-hint">Left mouse button: Add or Select · Right mouse button: Remove · Ctrl+Mouse wheel: Brim ear radius</div>
      {row('Brim ear radius', ui.radius, 0.1, 20, 0.1, (radius) => {
        setEarsUi({ radius });
        if (ui.selected.length) setEars(o.id, ears.map((e, i) => (ui.selected.includes(i) ? { ...e, r: radius } : e)), 'Change brim ear radius');
      })}
      <div className="cut-joint-section">Create</div>
      {row('Max angle', ui.maxAngle, 0, 180, 0.5, (maxAngle) => setEarsUi({ maxAngle }))}
      {row('Detection radius', ui.detection, 0, 100, 0.1, (detection) => setEarsUi({ detection }))}
      <button type="button" className="btn" title="Generate brim ears using Max angle and Detection radius" onClick={() => autoGenerate(o, ui.maxAngle, ui.detection, ui.radius)}>Auto-generate</button>
      <div className="cut-joint-section">Remove</div>
      <div className="cut-joint-buttons">
        <button type="button" className="btn" disabled={!ui.selected.length} onClick={() => { setEars(o.id, ears.filter((_, i) => !ui.selected.includes(i)), 'Delete brim ear'); setEarsUi({ selected: [] }); }}>Selected</button>
        <button type="button" className="btn" disabled={!ears.length} onClick={() => { setEars(o.id, [], 'Delete all brim ears'); setEarsUi({ selected: [] }); }}>All</button>
      </div>
      {!painted && ears.length > 0 && (
        <div className="brim-warning">
          Warning: The brim type is not set to "painted", the brim ears will not take effect!
          <button type="button" className="btn small" onClick={() => updateObject(o.id, { settings: { ...getState().objects.find((x) => x.id === o.id)!.settings, brim_type: 'painted' } }, 'Set brim type to painted')}>
            Set the brim type of this object to "painted"
          </button>
        </div>
      )}
      <div className="cut-joint-buttons"><span style={{ flex: 1 }} /><button type="button" className="btn primary" onClick={() => setState({ gizmo: 'none' })}>Done</button></div>
    </div>
  );
}
