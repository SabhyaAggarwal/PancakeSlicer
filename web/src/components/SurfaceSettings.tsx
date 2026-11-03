import { useEffect } from 'react';
import { probeEngine, useEngineHealth } from '../flow/engine-health';
import type { Config } from '../api/types';
import { OPTIONS } from '../options';
import { CURVED_DEFAULTS, WAVE_DEFAULTS } from '../intent/surface';
import { methodSelectionProblem, type MethodAvailability, type MethodAvailabilityResult } from '../profiles/capabilities';
import { getState, setState } from '../state/store';
import { toolArtifactManifest } from '../api/client';
import type { ProfileFields } from './SettingsTab';
import { Field } from './Field';
import './process-settings.css';

const shortReason = (m: MethodAvailability) => m.recovery === 'update_engine' ? (toolArtifactManifest ? 'Not in this engine build' : 'Engine still starting')
  : m.recovery === 'repair_printer' ? 'Printer axes unknown'
  : /rotary/i.test(m.reason ?? '') ? 'Needs rotary axes'
  : /Auto/.test(m.reason ?? '') ? 'Not guaranteed for this printer' : 'Not available';
const openMethodChoice = (key: string) => {
  const trigger = document.querySelector<HTMLElement>(`.opt-line[data-opt="${key}"] .dropdown`);
  trigger?.focus(); trigger?.click();
};
export const TOP_METHOD_KEYS = new Set(['nonplanar_mode','nonplanar_wave',...Object.keys(CURVED_DEFAULTS),...Object.keys(WAVE_DEFAULTS)]);
export function SurfaceSettings({config,system,availability,editor,dirty,readOnly = false}: {config:Config;system:Config;availability:MethodAvailabilityResult;editor?:ProfileFields;dirty:Config;readOnly?:boolean}) {
  const engine = useEngineHealth();
  useEffect(() => {void probeEngine();}, []);
  const mode = String(config.nonplanar_mode ?? OPTIONS.nonplanar_mode?.default ?? 'off');
  const wave = String(config.nonplanar_wave ?? OPTIONS.nonplanar_wave?.default ?? 'off');
  const problem = methodSelectionProblem(availability,config);
  const field = (key:string,label?:string,target?:'mode'|'wave') => {
    const def = OPTIONS[key]; if(!def) return null;
    const current = String(config[key] ?? def.default ?? 'off');
    const methods = target ? availability.methods.filter(m => m.target === target) : [];
    const values = target ? [...new Set([...(target === 'wave' ? ['off'] : []), ...methods.map(m => m.id), current])] : undefined;
    const labels = values?.map(value => key === 'nonplanar_mode' && value === 'off' ? 'Flat layers' : def.enum_labels?.[def.enum_values?.indexOf(value) ?? -1] ?? value);
    const blocked = values?.map(value => methods.find(m => m.id === value && !m.supported));
    return <Field key={key} type="process" optKey={key} label={label} value={config[key]} systemValue={system[key]} dirty={key in dirty} onSet={editor?.set} onReset={editor?.reset} disabled={readOnly}
      definition={values ? {enum_values:values,enum_labels:labels,enum_disabled:values.filter((_,i) => blocked![i]),enum_notes:blocked!.map((m, i) => m && values[i] !== current ? shortReason(m) : undefined)} : undefined}/>;
  };
  const notice = (target:'mode'|'wave') => {
    if (!problem || (target === 'mode' ? mode : wave) !== problem.method) return null;
    const method = availability.methods.find(m => m.target === target && m.id === problem.method);
    const key = target === 'mode' ? 'nonplanar_mode' : 'nonplanar_wave';
    return <div className="surface-why" role="alert"><span>{method ? shortReason(method) : 'Not available'}</span>
      {problem.recovery === 'repair_printer'
        ? <button type="button" className="link" onClick={() => setState({presetUx:{kind:'presets',type:'machine',name:getState().printer,edit:true}})}>Edit printer axes</button>
        : <button type="button" className="link" onClick={() => openMethodChoice(key)}>Choose another method</button>}
    </div>;
  };
  return <section className="optgroup surface-settings" data-engine-state={engine.value}>
    {field('nonplanar_mode','Slicing method','mode')}
    {notice('mode')}
    {mode === 'curved_tops' && availability.allowedModes.includes(mode) && Object.keys(CURVED_DEFAULTS).map(key=>field(key))}
    {(availability.allowedWaveModes.length > 0 || wave !== 'off') && field('nonplanar_wave','Wave pattern','wave')}
    {notice('wave')}
    {wave !== 'off' && availability.allowedWaveModes.includes(wave) && Object.keys(WAVE_DEFAULTS).map(key=>field(key))}
  </section>;
}
