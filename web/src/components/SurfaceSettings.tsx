import { useEffect } from 'react';
import { probeEngine, useEngineHealth } from '../flow/engine-health';
import type { Config } from '../api/types';
import { OPTIONS, modeVisible, type OptionMode } from '../options';
import { CURVED_DEFAULTS, WAVE_DEFAULTS } from '../intent/surface';
import { methodSelectionProblem, type MethodAvailability, type MethodAvailabilityResult } from '../profiles/capabilities';
import { getState, setState, useStore } from '../state/store';
import { openSlantPlane, slantTarget } from '../plater/slant';
import { toolArtifactManifest } from '../api/client';
import type { ProfileFields } from './SettingsTab';
import { Field } from './Field';
import { canonicalMode } from '../nonplanar-mode';
import './process-settings.css';

const shortReason = (m: MethodAvailability) => m.recovery === 'update_engine' ? (toolArtifactManifest ? 'Not in this engine build' : 'Engine still starting')
  : m.recovery === 'repair_printer' ? 'Printer axes unknown'
  : /rotary/i.test(m.reason ?? '') ? 'Needs rotary axes'
  : /Auto/.test(m.reason ?? '') ? 'Not guaranteed for this printer' : 'Not available';
const openMethodChoice = (key: string) => {
  const trigger = document.querySelector<HTMLElement>(`.opt-line[data-opt="${key}"] .dropdown`);
  trigger?.focus(); trigger?.click();
};
export { canonicalMode } from '../nonplanar-mode';
import { MODE_LABELS, MODE_HELP as BASE_MODE_HELP } from '../nonplanar-mode';
const OFFERED_MODES = ['off', 'curved_tops', 'nonplanar_top', 'smooth_morph', 'slant', 'conformal_layers'], ROTARY_MODES = ['multiplanar', 'field'];
const MODE_HELP: Record<string, string> = {
  ...BASE_MODE_HELP,
  multiplanar: 'Multiplanar: the part in chunks of flat layers, each chunk tilted so its overhangs need less support.',
  field: 'Rotary curved layers: the whole part in curved layers, the table tilting so each line is laid level under the nozzle.',
};
export function methodModes(current: string, availability: MethodAvailabilityResult): {enum_values: string[]; enum_labels: string[]; tooltip: string} {
  const offered = [...OFFERED_MODES, ...availability.printer.rotary ? ROTARY_MODES : []];
  const values = offered.includes(current) ? offered : [current, ...offered];
  const def = OPTIONS.nonplanar_mode;
  return {enum_values: values, enum_labels: values.map(v => MODE_LABELS[v] ?? def?.enum_labels?.[def.enum_values?.indexOf(v) ?? -1] ?? v),
    tooltip: offered.map(v => MODE_HELP[v]).join('\n')};
}
const DUCT_KEYS = ['nonplanar_ignore_duct', 'nonplanar_clearance_margin'];
export const TOP_METHOD_KEYS = new Set(['nonplanar_mode','nonplanar_wave',...Object.keys(CURVED_DEFAULTS),...DUCT_KEYS,...Object.keys(WAVE_DEFAULTS)]);
export function SurfaceSettings({config,system,availability,editor,dirty,uiMode,readOnly = false}: {config:Config;system:Config;availability:MethodAvailabilityResult;editor?:ProfileFields;dirty:Config;uiMode:OptionMode;readOnly?:boolean}) {
  const engine = useEngineHealth();
  useEffect(() => {void probeEngine();}, []);
  const mode = canonicalMode(String(config.nonplanar_mode ?? OPTIONS.nonplanar_mode?.default ?? 'off'));
  const slantObject = useStore((s) => !!slantTarget(s));
  const wave = String(config.nonplanar_wave ?? OPTIONS.nonplanar_wave?.default ?? 'off');
  const problem = methodSelectionProblem(availability,config);
  const field = (key:string,label?:string,target?:'mode'|'wave') => {
    const def = OPTIONS[key]; if(!def) return null;
    const current = key === 'nonplanar_mode' ? mode : String(config[key] ?? def.default ?? 'off');
    const methods = target ? availability.methods.filter(m => m.target === target) : [];
    const modes = target === 'mode' ? methodModes(current, availability) : undefined;
    const values = modes?.enum_values ?? (target ? [...new Set(['off', ...methods.map(m => m.id), current])] : undefined);
    const labels = modes?.enum_labels ?? values?.map(value => def.enum_labels?.[def.enum_values?.indexOf(value) ?? -1] ?? value);
    const blocked = values?.map(value => methods.find(m => m.id === value && !m.supported));
    return <Field key={key} type="process" optKey={key} label={label} value={key === 'nonplanar_mode' ? mode : config[key]} systemValue={system[key]} dirty={key in dirty} onSet={editor?.set} onReset={editor?.reset} disabled={readOnly} mode={mode}
      definition={values ? {...modes?.tooltip && {tooltip:modes.tooltip},enum_values:values,enum_labels:labels,enum_disabled:values.filter((_,i) => blocked![i]),enum_notes:blocked!.map((m, i) => m && values[i] !== current ? shortReason(m) : undefined)} : undefined}/>;
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
  const curved = (mode === 'curved_tops' || mode === 'nonplanar_top') && availability.allowedModes.includes(mode);
  const ductOn = ['1', 'true'].includes(String(config.nonplanar_ignore_duct ?? OPTIONS.nonplanar_ignore_duct?.default ?? '0'));
  const duct = modeVisible('develop', uiMode) ? [field('nonplanar_ignore_duct'), ductOn && field('nonplanar_clearance_margin')] : [];
  return <section className="optgroup surface-settings" data-engine-state={engine.value}>
    {field('nonplanar_mode','Slicing method','mode')}
    {notice('mode')}
    {curved && Object.keys(CURVED_DEFAULTS).flatMap(key => key === 'nonplanar_curved_max_angle' ? [field(key), ...duct] : [field(key)])}
    {mode === 'slant' && availability.allowedModes.includes(mode) && <>
      {field('nonplanar_slant_angle')}
      {field('nonplanar_slant_direction')}
      <div className="opt-line slant-define">
        <button type="button" className="btn" disabled={!slantObject || readOnly} title={slantObject ? undefined : 'Select an object to define its slant plane'}
          onClick={openSlantPlane}>Define slant plane</button>
      </div>
      {modeVisible('develop', uiMode) && [field('nonplanar_slant_start_height'), field('nonplanar_slant_morph_layers')]}
      {duct}
    </>}
    {(availability.allowedWaveModes.length > 0 || wave !== 'off') && field('nonplanar_wave','Wave pattern','wave')}
    {notice('wave')}
    {wave !== 'off' && availability.allowedWaveModes.includes(wave) && Object.keys(WAVE_DEFAULTS).map(key=>field(key))}
  </section>;
}
