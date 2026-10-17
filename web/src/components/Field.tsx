import { memo, useEffect, useState } from 'react';
import type { ConfigValue, PresetType } from '../api/types';
import { OPTIONS, splitKey, type OptionDef } from '../options';
import { resetOption, setOption } from '../state/store';
import { Icon } from './Icon';

const NUMERIC = new Set(['float', 'floats', 'int', 'ints', 'percent', 'percents', 'float_or_percent', 'floats_or_percents']);

function elementOf(v: ConfigValue | undefined, idx: number, vector: boolean): ConfigValue | undefined {
  if (!vector) return v;
  if (Array.isArray(v)) return v[Math.min(idx, v.length - 1)];
  if (typeof v === 'string' && v.includes(',') && !v.includes('\n')) return v.split(',')[idx] ?? v.split(',')[0];
  return v;
}

function withElement(v: ConfigValue | undefined, idx: number, el: ConfigValue): ConfigValue {
  const arr = Array.isArray(v) ? [...(v as (string | number)[])] : [];
  while (arr.length <= idx) arr.push(arr[0] ?? '');
  arr[idx] = el as string;
  return arr as string[];
}

const asText = (v: ConfigValue | undefined) => (v === undefined || v === null ? '' : Array.isArray(v) ? v.join(', ') : String(v));

export const Field = memo(function Field(props: {
  optKey: string;
  type: PresetType;
  value: ConfigValue | undefined;
  systemValue: ConfigValue | undefined;
  dirty: boolean;
}) {
  const [key, idx] = splitKey(props.optKey);
  const def: OptionDef | undefined = OPTIONS[key];
  if (!def) return null;
  const vector = def.type.endsWith('s') && def.type !== 'points' && def.type !== 'enums';
  const value = elementOf(props.value ?? (def.default as ConfigValue | undefined), idx, vector);
  const commit = (el: ConfigValue) => {
    const next = vector ? withElement(props.value, idx, el) : el;
    if (asText(next) === asText(props.systemValue)) resetOption(props.type, key);
    else setOption(props.type, key, next);
  };
  const label = def.label || def.full_label || key;
  const modified = props.dirty;
  const tooltip = `${def.tooltip ?? ''}\n\nparameter name: ${key}`.trim();

  return (
    <div className={`opt-line ${def.full_width || def.multiline ? 'full' : ''}`}>
      <span className={`opt-label ${modified ? 'modified' : ''}`} title={tooltip}>
        {label}
      </span>
      <span className="opt-field">
        <Widget def={def} value={value} onCommit={commit} />
        {def.sidetext && <span className="opt-side">{def.sidetext}</span>}
      </span>
      <span className="opt-undo">
        {modified && (
          <button type="button" className="icon-btn" title="Reset to the preset value" onClick={() => resetOption(props.type, key)}>
            <Icon name="undo" size={14} />
          </button>
        )}
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

function TextBox(props: { value: string; multiline?: boolean; numeric?: boolean; def?: OptionDef; readOnly?: boolean; onCommit: (v: ConfigValue) => void }) {
  const [text, setText] = useState(props.value);
  const [bad, setBad] = useState(false);
  useEffect(() => setText(props.value), [props.value]);
  const commit = () => {
    if (text === props.value) return;
    if (props.numeric && props.def) {
      const pct = text.trim().endsWith('%');
      const n = Number(text.replace('%', '').trim());
      const ok = Number.isFinite(n) && (props.def.min === undefined || n >= props.def.min) && (props.def.max === undefined || n <= props.def.max) &&
        (!pct || props.def.type.includes('percent'));
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
      onChange={(e) => {
        setText(e.target.value);
        setBad(false);
      }}
      onBlur={commit}
      onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
    />
  );
}
