
import type { ConfigValue } from '../api/types';
import { normalizeConfigValue } from '../config-values';
import { OPTIONS } from '../options';
import { PRESET_META } from './model';


export function display(key: string, value: ConfigValue | undefined): string {
  if (value === undefined) return '—';
  const def = OPTIONS[key];
  const v = normalizeConfigValue(key, value);
  const one = (x: unknown) => {
    if (x === null || x === 'nil') return def?.nullable ? 'Inherited' : 'nil';
    const text = String(x);
    if (def?.type.startsWith('bool')) return text === '1' || text === 'true' ? 'On' : 'Off';
    const i = def?.enum_values?.indexOf(text) ?? -1;
    return i >= 0 ? def!.enum_labels?.[i] ?? text : text;
  };
  return Array.isArray(v) ? v.map(one).join(', ') : one(v);
}


export const presetLabel = (name: string) =>
  (name.replace(/\s*@.*$/, '').replace(/\s*[·-]\s*System\b/gi, '').trim() || name);


export const isMetaKey = (key: string) => PRESET_META.test(key) || /^(inherits_group|different_settings_to_system)$/.test(key);
