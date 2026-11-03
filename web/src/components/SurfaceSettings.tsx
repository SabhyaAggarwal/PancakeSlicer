import { useEffect } from 'react';
import { probeEngine, useEngineHealth } from '../flow/engine-health';
import type { Config } from '../api/types';
import { OPTIONS } from '../options';
import { CURVED_DEFAULTS, WAVE_DEFAULTS } from '../intent/surface';
import { methodSelectionProblem, type MethodAvailabilityResult } from '../profiles/capabilities';
import type { ProfileFields } from './SettingsTab';
import { Field } from './Field';
import './process-settings.css';

export const TOP_METHOD_KEYS = new Set(['nonplanar_mode','nonplanar_wave',...Object.keys(CURVED_DEFAULTS),...Object.keys(WAVE_DEFAULTS)]);
export function SurfaceSettings({config,system,availability,editor,dirty,readOnly = false}: {config:Config;system:Config;availability:MethodAvailabilityResult;editor?:ProfileFields;dirty:Config;readOnly?:boolean}) {
  const engine = useEngineHealth();
  useEffect(() => {void probeEngine();}, []);
  const mode = String(config.nonplanar_mode ?? OPTIONS.nonplanar_mode?.default ?? 'off');
  const wave = String(config.nonplanar_wave ?? OPTIONS.nonplanar_wave?.default ?? 'off');
  const problem = methodSelectionProblem(availability,config);
  const field = (key:string,label?:string,allowed?:string[]) => {
    const def = OPTIONS[key]; if(!def) return null;
    const current = String(config[key] ?? def.default ?? 'off');
    const unsupported = allowed && !allowed.includes(current);
    const values = allowed ? [...new Set([...allowed,...(unsupported ? [current] : [])])] : undefined;
    const labels = values?.map(value => key === 'nonplanar_mode' && value === 'off' ? 'Flat layers' : def.enum_labels?.[def.enum_values?.indexOf(value) ?? -1] ?? value);
    return <Field key={key} type="process" optKey={key} label={label} value={config[key]} systemValue={system[key]} dirty={key in dirty} onSet={editor?.set} onReset={editor?.reset} disabled={readOnly} definition={values ? {enum_values:values,enum_labels:labels,enum_disabled:unsupported ? [current] : []} : undefined}/>;
  };
  return <section className="optgroup surface-settings" data-engine-state={engine.value}>
    {field('nonplanar_mode','Slicing method',availability.allowedModes)}
    {mode === 'curved_tops' && availability.allowedModes.includes(mode) && Object.keys(CURVED_DEFAULTS).map(key=>field(key))}
    {(availability.allowedWaveModes.length > 0 || wave !== 'off') && field('nonplanar_wave','Wave pattern',['off',...availability.allowedWaveModes])}
    {wave !== 'off' && availability.allowedWaveModes.includes(wave) && Object.keys(WAVE_DEFAULTS).map(key=>field(key))}
    {problem && <div className="surface-why" role="alert">{problem.reason} Choose a supported method or update the printer and engine configuration.</div>}
  </section>;
}
