import { Check } from './Check';
import { ColorPicker } from './ColorPicker';
import { Dropdown, EditableDropdown } from './Dropdown';
import { createContext, memo, useContext, useEffect, useId, useRef, useState } from 'react';
import type { ConfigValue, PresetType } from '../api/types';
import { normalizeConfigValue, toolValue } from '../config-values';
import { OPTIONS, splitKey, type OptionDef } from '../options';
import { effectiveConfig, getState, resetOption, setOption, useStore } from '../state/store';
import { Icon } from './Icon';
import { InfoTip } from './InfoTip';
import { Switch } from './fields';
import { Choice } from './kit';
import { displayLabel } from './fields/labels';
import { ScopeMark } from '../profiles/Scope';
import type { ChangeTarget } from '../state/changes';

const LabelId = createContext<string | undefined>(undefined);
const useLabel = () => ({ 'aria-labelledby': useContext(LabelId) });

const NUMERIC = new Set(['float', 'floats', 'int', 'ints', 'percent', 'percents', 'float_or_percent', 'floats_or_percents']);

function elementOf(v: ConfigValue | undefined, idx: number, vector: boolean): ConfigValue | undefined {
  if (!vector) return v;
  if (Array.isArray(v)) return v[Math.min(idx, v.length - 1)];
  return v;
}

function withElement(v: ConfigValue | undefined, idx: number, el: ConfigValue): ConfigValue {
  const arr = Array.isArray(v) ? [...v] : [];
  while (arr.length <= idx) arr.push(arr[0] ?? '');
  arr[idx] = el as string;
  return arr as string[];
}

const asText = (v: unknown): string => v == null ? '' : Array.isArray(v) ? v.map(asText).join(', ') : typeof v === 'object' && 'value' in v ? `${v.value}${'percent' in v && v.percent ? '%' : ''}` : String(v);

const CHOICE_HINTS: Record<string, Record<string, string>> = {
  nonplanar_mode: {
    off: 'Standard: flat layers, like any slicer.',
    auto: 'Auto: chooses per part between curved tops, whole-part curved layers and flat.',
    manual: 'Custom: the curved-layer settings below, exactly as set.',
    multiplanar: 'Tilted chunks: the bed tilts so each chunk prints its overhangs upright.',
    field: 'Rotary curved layers: every layer curved, the table keeping each line level.',
    oneshot: 'One-shot: the solver plans layers for this part (slow).',
  },
};

const PROMPTED = new Set(['spiral_mode', 'wall_loops', 'top_shell_layers', 'sparse_infill_density', 'enable_support', 'enforce_support_layers', 'detect_thin_wall',
  'overhang_reverse', 'timelapse_type', 'enable_wrapping_detection', 'layer_height', 'nozzle_temperature', 'nozzle_temperature_initial_layer',
  'nozzle_temperature_range_low', 'nozzle_temperature_range_high', 'alternate_extra_wall', 'ensure_vertical_shell_thickness', 'seam_slope_type',
  'seam_slope_start_height', 'filament_max_volumetric_speed']);

export const Field = memo(function Field(props: {
  optKey: string;
  type: PresetType;
  slot?: number;
  value: ConfigValue | undefined;
  systemValue: ConfigValue | undefined;
  dirty: boolean;
  disabled?: boolean;
  label?: string;
  hideLabel?: boolean;
  tooltip?: string;
  definition?: Partial<OptionDef>;
  inheritedValue?: ConfigValue;
  overrideDisabled?: boolean;
  onSet?: (key: string, value: ConfigValue) => void;
  onReset?: (key: string) => void;
  resetTitle?: string;
  extra?: React.ReactNode;
  scope?: { target?: ChangeTarget };
  widget?: React.ReactNode;
}) {
  const [key, idx] = splitKey(props.optKey);
  const labelId = useId();
  const baseDef = OPTIONS[key];
  if (!baseDef) return null;
  const def: OptionDef = { ...baseDef, ...props.definition };
  const vector = def.type.endsWith('s') && def.type !== 'points' && def.type !== 'points_groups' && def.type !== 'ints_groups';
  const current = props.value === undefined ? undefined : normalizeConfigValue(key, props.value);
  const system = props.systemValue === undefined ? undefined : normalizeConfigValue(key, props.systemValue);
  const value = elementOf(current ?? (def.nullable ? undefined : def.default as ConfigValue | undefined), idx, vector);
  const prompt = (k: string) => { if (PROMPTED.has(k)) void import('../configPrompts').then((m) => m.checkSetting(props.type, k, props.slot)); };
  const set = props.onSet ?? ((k: string, v: ConfigValue) => { setOption(props.type, k, v, props.slot); prompt(k); });
  const reset = props.onReset ?? ((k: string) => { resetOption(props.type, k, props.slot); prompt(k); });
  const commit = (el: ConfigValue) => {
    const next = normalizeConfigValue(key, vector ? withElement(current ?? system ?? def.default as ConfigValue, idx, el) : el);
    if (system !== undefined && toolValue(key, next) === toolValue(key, system) && !props.onSet) reset(key);
    else set(key, next);
  };
  const fullLabel = props.label ?? (def.label || def.full_label || key);
  const label = props.label === undefined ? displayLabel(key, fullLabel) : fullLabel;
  const modified = props.dirty;
  const checked = !props.overrideDisabled && value != null && value !== 'nil' && value !== '';
  const inherited = props.inheritedValue ?? OPTIONS[key.replace(/^filament_/, '')]?.default;
  const fallback = elementOf(inherited as ConfigValue | undefined, idx, vector) ?? '0';
  const isSwitch = def.type.replace(/s$/, '') === 'bool' && !FILAMENT_SLOT_KEYS.has(key);
  const isOn = value === true || value === 'true' || value === '1' || value === 1;
  const locked = !!props.disabled || !!def.readonly || (def.nullable && !checked);
  const disabledReason = def.readonly ? 'This value is read-only for the selected profile.' : props.disabled ? 'This setting is unavailable with the current printer and process options.' : def.nullable && !checked ? 'Enable the override to edit this value.' : undefined;
  const hint = CHOICE_HINTS[key]?.[asText(value)];
  const help = props.tooltip || def.tooltip;

  return (
    <div className={`opt-line ${def.full_width || def.multiline ? 'full' : ''} ${isSwitch ? 'is-switch' : ''}`} data-opt={key} onClick={isSwitch && !locked ? (e) => {
      if (!(e.target as HTMLElement).closest('input, button')) commit(isOn ? '0' : '1');
    } : undefined}>
      <span className={`opt-label ${modified ? 'modified' : ''} ${props.hideLabel ? 'field-label-hidden' : ''}`} id={labelId}>
        {def.nullable && <Check className="nullable-toggle" aria-label={`Override ${label}`} checked={checked} disabled={props.overrideDisabled}
        onCheckedChange={(next) => commit(next ? fallback : 'nil')} />}
        {label}
        {!props.hideLabel && (help || hint) && <InfoTip label={fullLabel}>{label !== fullLabel && <strong>{fullLabel}</strong>}{hint && <p>{hint}</p>}{help && <p>{help}</p>}<code>{key}</code></InfoTip>}
      </span>
      <fieldset className="opt-field" aria-description={disabledReason} disabled={props.disabled || (def.nullable && !checked)}>
        <LabelId.Provider value={labelId}>
          {props.widget ?? <Widget def={def} value={def.nullable && !checked ? fallback : value} onCommit={commit} />}
        </LabelId.Provider>
        {def.sidetext && (props.widget || !NUMERIC.has(def.type)) && <span className="opt-side">{def.sidetext}</span>}
      </fieldset>
      <span className="opt-undo">
        {props.scope ? <ScopeMark type={props.type} slot={props.slot} optKey={key} target={props.scope.target} /> : <>
        {modified && (
          <button type="button" className="icon-btn" aria-label={`${props.resetTitle ?? 'Reset to the preset value'}: ${label}`} onClick={() => reset(key)}>
            <Icon name="undo" size={14} />
          </button>
        )}
        {props.extra}</>}
      </span>
    </div>
  );
});

const FILAMENT_SLOT_KEYS = new Set(['support_filament', 'support_interface_filament', 'wipe_tower_filament', 'outer_wall_filament_id',
  'inner_wall_filament_id', 'sparse_infill_filament_id', 'internal_solid_filament_id', 'top_surface_filament_id', 'bottom_surface_filament_id']);

function SlotChoice({ value, onCommit }: { value: string; onCommit: (v: ConfigValue) => void }) {
  const slots = useStore((s) => s.filamentSlots);
  useStore((s) => s.filamentEdits);
  const type = (i: number) => { const t = effectiveConfig(getState(), 'filament', i).filament_type; return String(Array.isArray(t) ? t[0] : t ?? ''); };
  const known = value === '0' || (Number(value) >= 1 && Number(value) <= slots.length);
  return (
    <Dropdown className="combo" {...useLabel()} value={value} onValueChange={onCommit} options={[{ value: "0", label: 'Default' }, ...slots.map((_, i) => ({ value: String(i + 1), label: <>{i + 1}: {type(i)}</> })), ...(!known ? [{ value, label: value }] : [])]} />
  );
}

function Widget({ def, value, onCommit }: { def: OptionDef; value: ConfigValue | undefined; onCommit: (v: ConfigValue) => void }) {
  const named = useLabel();
  const base = def.type.replace(/s$/, '');
  if (FILAMENT_SLOT_KEYS.has(def.key)) return <SlotChoice value={asText(value) || '0'} onCommit={onCommit} />;
  if (base === 'bool') {
    const on = value === true || value === 'true' || value === '1' || value === 1;
    return (
      <Switch {...named} checked={on} disabled={def.readonly} onChange={(v) => onCommit(v ? '1' : '0')} />
    );
  }
  if (def.enum_values?.length && /^(select_open|[if]_enum_open)$/.test(def.gui_type ?? '')) {
    return <EditableDropdown {...named} value={asText(value)} readOnly={def.readonly} onValueChange={onCommit} className="text" options={def.enum_values.map((v,i) => ({value: v, label: def.enum_labels?.[i] ?? v}))} />;
  }
  if ((base === 'enum' || def.enum_values) && def.enum_values?.length) {
    const v = asText(value);
    const known = def.enum_values.includes(v);
    const labels = def.enum_values.map((ev, i) => def.enum_labels?.[i] ?? ev);
    if (known && !def.enum_notes?.some(Boolean) && segmented(labels)) return <Choice label={def.full_label || def.label || def.key} value={v} disabled={def.readonly} onChange={onCommit}
      options={def.enum_values.map((ev, i) => ({ id: ev, label: labels[i], disabled: def.enum_disabled?.includes(ev) }))} />;
    return (
      <Dropdown className="combo" {...named} value={v} disabled={def.readonly} onValueChange={onCommit} options={[...(!known ? [{ value: v, label: v }] : []), ...def.enum_values.map((ev, i) => (
          ({ value: ev, label: def.enum_notes?.[i] ? <>{labels[i]}<span className="menu-note">{def.enum_notes[i]}</span></> : labels[i], search: labels[i], disabled: def.enum_disabled?.includes(ev) })
        ))]} />
    );
  }
  if (def.gui_type === 'color' || /colou?r$/.test(def.key)) {
    const v = asText(value) || '#000000';
    return <ColorPicker {...named} label={def.label || def.full_label || "Color"} value={v.slice(0, 7)} onCommit={onCommit} disabled={def.readonly}/>;
  }
  if (def.multiline || (base === 'string' && /gcode|notes|post_process/.test(def.key))) {
    return <TextBox value={asText(value)} multiline onCommit={onCommit} readOnly={def.readonly} />;
  }
  return <TextBox value={asText(value)} numeric={NUMERIC.has(def.type)} spin={base === 'int'} def={def} onCommit={onCommit} readOnly={def.readonly} />;
}

const segmented = (labels: string[]) => labels.length >= 2 && labels.length <= 4 && labels.every((l) => l.trim())
  && labels.reduce((w, l) => w + l.length * 7 + 16, 0) <= 150;

const floatStep = (n: number) => (n ? 10 ** (Math.floor(Math.log10(Math.abs(n))) - 1) : 0.1);

function TextBox(props: { value: string; multiline?: boolean; numeric?: boolean; spin?: boolean; def?: OptionDef; readOnly?: boolean; list?: string; onCommit: (v: ConfigValue) => void }) {
  const named = useLabel();
  const [text, setText] = useState(props.value);
  const [bad, setBad] = useState(false);
  const latest = useRef(text);
  latest.current = text;
  useEffect(() => setText(props.value), [props.value]);
  const commit = () => {
    const text = latest.current;
    if (text === props.value) return;
    if (props.numeric && props.def) {
      const pct = text.trim().endsWith('%');
      const n = Number(text.replace('%', '').trim());
      const ok = Number.isFinite(n) && (props.def.min === undefined || n >= props.def.min) && (props.def.max === undefined || n <= props.def.max) &&
        (!pct || props.def.type.includes('percent')) && (!props.def.type.startsWith('int') || Number.isInteger(n));
      setBad(!ok);
      if (!ok) return;
    }
    props.onCommit(text);
  };
  const stepNumber = (direction: number, big = false) => {
    if (props.readOnly || !props.def) return;
    const raw = latest.current.trim(), pct = raw.endsWith('%'), n = Number(raw.replace('%', '')) || 0;
    const step = (props.spin || props.def.type.startsWith('int') ? 1 : floatStep(n)) * (big ? 10 : 1);
    const places = Math.max(0, -Math.floor(Math.log10(step)));
    const next = Math.min(props.def.max ?? Infinity, Math.max(props.def.min ?? -Infinity, Number((n + direction * step).toFixed(places))));
    const value = `${next}${pct ? '%' : ''}`;
    latest.current = value; setText(value); setBad(false); props.onCommit(value);
  };
  const input = useRef<HTMLInputElement>(null), stepRef = useRef(stepNumber);
  stepRef.current = stepNumber;
  useEffect(() => {
    const el = input.current;
    if (!el || !props.numeric) return;
    const wheel = (e: WheelEvent) => { if (document.activeElement !== el || !e.deltaY) return; e.preventDefault(); stepRef.current(e.deltaY < 0 ? 1 : -1, e.shiftKey); };
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  }, [props.numeric]);
  if (props.multiline)
    return <textarea className="text multiline" {...named} value={text} readOnly={props.readOnly} spellCheck={false} onChange={(e) => setText(e.target.value)} onBlur={commit} />;
  if (!props.numeric) return (
    <span className="control-text">
      <input {...named} className="text" value={text} readOnly={props.readOnly} list={props.list}
        onChange={(e) => { setText(e.target.value); setBad(false); }} onBlur={commit} onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()} />
    </span>
  );
  const unit = props.def?.sidetext;
  return (
    <span className="control-number" style={unit ? { '--unit-ch': unit.length } as React.CSSProperties : undefined}>
      <input
        {...named}
        ref={input}
        aria-description={unit}
        inputMode={props.def?.min !== undefined && props.def.min >= 0 ? 'decimal' : undefined}
        className={`text num ${unit ? 'has-unit' : ''} ${bad ? 'bad' : ''}`}
        value={text}
        readOnly={props.readOnly}
        onChange={(e) => { setText(e.target.value); setBad(false); }}
        onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
          else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') { e.preventDefault(); stepNumber(e.key === 'ArrowUp' ? 1 : -1, e.shiftKey); }
        }}
      />
      {unit && <span className="control-unit" aria-hidden>{unit}</span>}
      <span className="number-steps"><button type="button" tabIndex={-1} aria-label="Increase value" disabled={props.readOnly} onMouseDown={e => e.preventDefault()} onClick={(e) => stepNumber(1, e.shiftKey)}>+</button><button type="button" tabIndex={-1} aria-label="Decrease value" disabled={props.readOnly} onMouseDown={e => e.preventDefault()} onClick={(e) => stepNumber(-1, e.shiftKey)}>−</button></span>
    </span>
  );
}
