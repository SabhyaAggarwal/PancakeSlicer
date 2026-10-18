import type { ConfigValue } from './api/types';
import type { OptionDef, OptionMode } from './options';
import { normalizeConfigValue, setOptionTypes, unescapeConfigString, vectorType } from './config-values';

const MODES: Record<string, OptionMode> = {
  Simple: 'simple', Advanced: 'advanced', Expert: 'develop', Develop: 'develop',
  0: 'simple', 1: 'advanced', 2: 'develop', 3: 'develop',
};

interface Metadata {
  type: string;
  mode: string | number;
  enum_values?: unknown[];
  default?: unknown;
  type_id?: number;
  [key: string]: unknown;
}

function defaultValue(def: Metadata): unknown {
  const type = vectorType(def.type);
  if (type) return def.default == null ? def.default : normalizeConfigValue('', def.default as ConfigValue, type);
  if (def.type_id === undefined || typeof def.default !== 'string') return def.default;
  const value = def.default;
  const name = def.type.replace(/^co/, '').replace(/_/g, '').toLowerCase();
  if (name === 'string') return unescapeConfigString(value);
  if (value === '' && name !== 'enum') return null;
  if (name === 'bool') return value === '1' || value === 'true';
  if (name === 'float' || name === 'int' || name === 'percent') return Number(value.replace(/%$/, ''));
  if (name === 'floatorpercent') return { value: Number(value.replace(/%$/, '')), percent: value.endsWith('%') };
  if (name === 'point' || name === 'point3') return value.split(/[,x]/).map(Number);
  return value;
}

export function normalizeOptions(inventory: Record<string, Metadata>): Record<string, OptionDef> {
  return Object.fromEntries(Object.entries(inventory).map(([key, def]) => [key, {
    ...def,
    key,
    enum_values: (def.enum_values ?? []).filter((value): value is string => typeof value === 'string'),
    type: def.type.replace(/^co/, '').replace(/Or/g, '_or_').replace(/Groups/g, '_groups').toLowerCase(),
    mode: MODES[def.mode],
    default: defaultValue(def),
  }]));
}

function records(raw: any): Record<string, Metadata> {
  const options = raw?.schema === 'orca.options.v1' && Array.isArray(raw.options)
    ? Object.fromEntries(raw.options.map((def: any) => [def.key, def])) : raw;
  if (!options || typeof options !== 'object' || !Object.keys(options).length ||
    Object.values(options).some((def: any) => !def || typeof def.type !== 'string' || MODES[def.mode] === undefined))
    throw new Error('Invalid option dictionary');
  return options;
}

async function asset(url: string): Promise<any> {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}: ${url}`);
  return response.json();
}

export async function loadRuntimeOptions(url: string, mock = false): Promise<Record<string, OptionDef>> {
  const shippedPromise = asset(url).then(records).catch(() => null);
  let native = false;
  if (!mock) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 1500);
    try {
      const health = await fetch('/api/healthz', { signal: controller.signal }).then((r) => r.ok ? r.json() : null);
      native = health?.tool_features?.dump_options === true;
    } catch {
    } finally { clearTimeout(timer); }
  }
  const selected = native ? records(await asset('/api/options')) : await shippedPromise;
  if (!selected) throw new Error('Shipped option dictionary unavailable');
  const options = normalizeOptions(selected);
  const shipped = await shippedPromise;
  const hints = shipped ? normalizeOptions(shipped) : {};
  if (native) for (const [key, def] of Object.entries(options)) {
    const hint = hints[key];
    if (!hint || hint.type !== def.type) continue;
    for (const name of ['gui_type', 'multiline', 'full_width'] as const)
      if (hint[name] !== undefined) Object.assign(def, { [name]: hint[name] });
  }
  setOptionTypes(options);
  return options;
}
