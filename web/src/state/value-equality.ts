
import type { ConfigValue } from '../api/types';
import { normalizeConfigValue, toolValue } from '../config-values';
import { OPTIONS } from '../options';

const text = (key: string, value: ConfigValue | undefined): string | undefined => {
  if (value === undefined) return undefined;
  const def = OPTIONS[key];
  const normalized = normalizeConfigValue(key, value);
  const scalar = (v: unknown) => {
    if (v === null || v === 'nil') return 'nil';
    if (def?.type.startsWith('bool')) return v === true || v === 1 || v === '1' || v === 'true' ? '1' : '0';
    if (/^(float|int|percent)/.test(def?.type ?? '')) {
      const raw = String(v).trim(), pct = raw.endsWith('%');
      const number = Number(pct ? raw.slice(0, -1) : raw);
      if (raw && Number.isFinite(number)) return `${number}${pct && /^floats?_or_percents?$/.test(def?.type ?? '') ? '%' : ''}`;
    }
    return v;
  };
  return toolValue(key, (Array.isArray(normalized) ? normalized.map(scalar) : scalar(normalized)) as ConfigValue);
};

export const sameValue = (key: string, a: ConfigValue | undefined, b: ConfigValue | undefined) => text(key, a) === text(key, b);
