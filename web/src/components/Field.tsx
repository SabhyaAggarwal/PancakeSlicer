import { memo, useEffect, useState } from 'react';
import type { ConfigValue, PresetType } from '../api/types';
import { normalizeConfigValue, toolValue } from '../config-values';
import { OPTIONS, splitKey, type OptionDef } from '../options';
import { resetOption, setOption } from '../state/store';
import { Icon } from './Icon';

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

export const Field = memo(function Field(props: {
  optKey: string;
  type: PresetType;
  slot?: number;
  value: ConfigValue | undefined;
  systemValue: ConfigValue | undefined;
  dirty: boolean;
  disabled?: boolean;
  label?: string;
  tooltip?: string;
  definition?: Partial<OptionDef>;
  inheritedValue?: ConfigValue;
  overrideDisabled?: boolean;
  onSet?: (key: string, value: ConfigValue) => void;
  onReset?: (key: string) => void;
  resetTitle?: string;
  extra?: React.ReactNode;
  widget?: React.ReactNode;
}) {
  const [key, idx] = splitKey(props.optKey);
  const baseDef = OPTIONS[key];
  if (!baseDef) return null;
  const def: OptionDef = { ...baseDef, ...props.definition };
  const vector = def.type.endsWith('s') && def.type !== 'points' && def.type !== 'points_groups' && def.type !== 'ints_groups';
  const current = props.value === undefined ? undefined : normalizeConfigValue(key, props.value);
  const system = props.systemValue === undefined ? undefined : normalizeConfigValue(key, props.systemValue);
  const value = elementOf(current ?? (def.nullable ? undefined : def.default as ConfigValue | undefined), idx, vector);
  const set = props.onSet ?? ((k: string, v: ConfigValue) => setOption(props.type, k, v, props.slot));
  const reset = props.onReset ?? ((k: string) => resetOption(props.type, k, props.slot));
  const commit = (el: ConfigValue) => {
    const next = normalizeConfigValue(key, vector ? withElement(current ?? system ?? def.default as ConfigValue, idx, el) : el);
    if (system !== undefined && toolValue(key, next) === toolValue(key, system) && !props.onSet) reset(key);
    else set(key, next);
  };
  const label = props.label ?? (def.label || def.full_label || key);
  const modified = props.dirty;
  const checked = !props.overrideDisabled && value != null && value !== 'nil' && value !== '';
  const inherited = props.inheritedValue ?? OPTIONS[key.replace(/^filament_/, '')]?.default;
  const fallback = elementOf(inherited as ConfigValue | undefined, idx, vector) ?? '0';
  const tooltip = `${props.tooltip ?? def.tooltip ?? ''}\n\nparameter name: ${key}`.trim();

  return (
    <div className={`opt-line ${def.full_width || def.multiline ? 'full' : ''}`}>
      <span className={`opt-label ${modified ? 'modified' : ''}`} title={tooltip}>
        {def.nullable && <input className="nullable-toggle" type="checkbox" aria-label={`Override ${label}`} checked={checked} disabled={props.overrideDisabled}
        onChange={(e) => commit(e.target.checked ? fallback : 'nil')} />}
        {label}
      </span>
      <fieldset className="opt-field" disabled={props.disabled || (def.nullable && !checked)}>
        {props.widget ?? <Widget def={def} value={def.nullable && !checked ? fallback : value} onCommit={commit} />}
        {def.sidetext && <span className="opt-side">{def.sidetext}</span>}
      </fieldset>
      <span className="opt-undo">
        {modified && (
          <button type="button" className="icon-btn" title={props.resetTitle ?? 'Reset to the preset value'} onClick={() => reset(key)}>
            <Icon name="undo" size={14} />
          </button>
        )}
        {props.extra}
      </span>
    </div>
  );
});

function Widget({ def, value, onCommit }: { def: OptionDef; value: ConfigValue | undefined; onCommit: (v: ConfigValue) => void }) {
  const base = def.type.replace(/s$/, '');
  if (base === 'bool') {
    const on = value === true || value === 'true' || value === '1' || value === 1;
    return (
      <input type="checkbox" className="chk" checked={on} disabled={def.readonly} onChange={(e) => onCommit(e.target.checked ? '1' : '0')} />
    );
  }
  if (def.enum_values?.length && /^(select_open|[if]_enum_open)$/.test(def.gui_type ?? '')) {
    return <><TextBox value={asText(value)} onCommit={onCommit} readOnly={def.readonly} list={`choices-${def.key}`} />
      <datalist id={`choices-${def.key}`}>{def.enum_values.map((v, i) => <option key={v} value={v}>{def.enum_labels?.[i] ?? v}</option>)}</datalist></>;
  }
  if ((base === 'enum' || def.enum_values) && def.enum_values?.length) {
    const v = asText(value);
    const known = def.enum_values.includes(v);
    return (
      <select className="combo" value={v} disabled={def.readonly} onChange={(e) => onCommit(e.target.value)}>
        {!known && <option value={v}>{v}</option>}
        {def.enum_values.map((ev, i) => (
          <option key={ev} value={ev}>
            {def.enum_labels?.[i] ?? ev}
          </option>
        ))}
      </select>
    );
  }
  if (def.gui_type === 'color' || /colou?r$/.test(def.key)) {
    const v = asText(value) || '#000000';
    return <input type="color" className="color" value={v.slice(0, 7)} onChange={(e) => onCommit(e.target.value.toUpperCase())} />;
  }
  if (def.multiline || (base === 'string' && /gcode|notes|post_process/.test(def.key))) {
    return <TextBox value={asText(value)} multiline onCommit={onCommit} readOnly={def.readonly} />;
  }
  return <TextBox value={asText(value)} numeric={NUMERIC.has(def.type)} def={def} onCommit={onCommit} readOnly={def.readonly} />;
}

function TextBox(props: { value: string; multiline?: boolean; numeric?: boolean; def?: OptionDef; readOnly?: boolean; list?: string; onCommit: (v: ConfigValue) => void }) {
  const [text, setText] = useState(props.value);
  const [bad, setBad] = useState(false);
  useEffect(() => setText(props.value), [props.value]);
  const commit = () => {
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
  if (props.multiline)
    return <textarea className="text multiline" value={text} readOnly={props.readOnly} spellCheck={false} onChange={(e) => setText(e.target.value)} onBlur={commit} />;
  return (
    <input
      className={`text ${props.numeric ? 'num' : ''} ${bad ? 'bad' : ''}`}
      value={text}
      readOnly={props.readOnly}
      list={props.list}
      onChange={(e) => {
        setText(e.target.value);
        setBad(false);
      }}
      onBlur={commit}
      onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
    />
  );
}
