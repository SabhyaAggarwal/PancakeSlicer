import { useEffect } from 'react';
import { probeEngine, useEngineHealth } from '../flow/engine-health';
import { Dropdown } from './Dropdown';
import type { Config } from '../api/types';
import { OPTIONS } from '../options';
import { applySurface, CURVED_DEFAULTS, SURFACE_KEYS, surfaceOf, surfaceUnavailable, WAVE_DEFAULTS, type SurfaceId } from '../intent/surface';
import { undoable } from '../state/history';
import { effectiveMode, useStore } from '../state/store';
import { revert } from '../profiles/stack';
import { useChanges } from '../state/useChanges';
import { Field } from './Field';
import { Icon } from './Icon';
import './process-settings.css';

const fieldLabels: Record<string, string> = {
  nonplanar_max_slope: 'Steepest curve', nonplanar_max_patch_height: 'Curve height', nonplanar_top_shells: 'Curved top layers',
  nonplanar_wave_amplitude: 'Ripple height', nonplanar_wave_wavelength: 'Ripple spacing', nonplanar_wave_direction: 'Ripple direction',
};

export function SurfaceSettings({ config, system }: { config: Config; system: Config }) {
  const engine = useEngineHealth();
  useEffect(() => { void probeEngine(); }, []);
  const current = surfaceOf(config);
  const advanced = useStore(effectiveMode) !== 'simple';
  const changes = useChanges('process');
  const own = changes.filter((c) => c.scope === 'project' && SURFACE_KEYS.includes(c.key));
  const numeric = current === 'smooth' ? CURVED_DEFAULTS : current === 'waves' ? WAVE_DEFAULTS : {};
  const noCurved = surfaceUnavailable('smooth');
  const noWaves = surfaceUnavailable('waves');
  const choice = (id: SurfaceId, label: string, hint: string, off?: string) => <button type="button" key={id} role="radio" aria-checked={current === id || (id === 'flat' && current === 'waves')}
    aria-disabled={!!off} className={off ? 'off' : undefined} title={off ? `${label} is unavailable: ${off}` : hint} onClick={() => !off && applySurface(id)}>{label}</button>;
  return <section className="optgroup surface-settings" data-opt="nonplanar_mode" data-engine-state={engine.value}>
    <div className="opt-line surface-title-row">
      <span className="opt-label" title="Shape of the top layers">Top surface</span>
      <div className="opt-field">
        <div className="seg surface-segments" role="radiogroup" aria-label="Top surface">
          {choice('flat', 'Flat', 'Flat: every layer lies in one plane, like any slicer.')}
          {choice('smooth', 'Curved', 'Curved: the top layers follow the shape of the part, for smoother tops.', noCurved)}
        </div>
      </div>
      {own.length > 0 && <span className="opt-undo"><button type="button" className="icon-btn" title="Use preset value" aria-label="Use preset value: Top surface"
        onClick={() => undoable('Revert top surface', () => own.forEach(revert))}><Icon name="undo" size={14}/></button></span>}
    </div>
    {noCurved && <p className="surface-why" role="note">Curved layers: {noCurved.charAt(0).toLowerCase() + noCurved.slice(1)}</p>}
    {advanced && current !== 'smooth' && <div className="opt-line" data-opt="nonplanar_wave">
      <span className="opt-label">Layer pattern</span>
      <Dropdown className="surface-pattern" aria-label="Layer pattern" value={current === 'waves' ? 'waves' : 'flat'} disabled={current === 'custom' || !!noWaves} title={noWaves} onValueChange={(v) => applySurface(v as SurfaceId)} options={[{value: 'flat', label: 'Plain'}, {value: 'waves', label: 'Whole waves'}]} />
    </div>}
    {Object.keys(numeric).filter((key) => OPTIONS[key]).map((key) => <Field key={key} type="process" optKey={key} label={fieldLabels[key]}
      value={config[key]} systemValue={system[key]} dirty={own.some((c) => c.key === key)}
      onReset={(key) => { const c = own.find((c) => c.key === key); if (c) revert(c); }}
      disabled={!!surfaceUnavailable(current as SurfaceId)} />)}
  </section>;
}
