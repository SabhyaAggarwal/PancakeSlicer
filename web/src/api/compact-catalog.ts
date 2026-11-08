import type { Config, ConfigValue, PresetCatalog, SystemPreset } from './types';

type CompactPreset = Omit<SystemPreset, 'config'> & { base: number; config: number[] };
export interface CompactCatalog {
  machines: CompactPreset[];
  processes: CompactPreset[];
  filaments: CompactPreset[];
  bases: number[][];
  keys: string[];
  values: ConfigValue[];
}

export function compactCatalog(catalog: PresetCatalog): CompactCatalog {
  const result = { bases: [], keys: [], values: [] } as unknown as CompactCatalog;
  const keys = new Map<string, number>();
  const values = new Map<string, number>();
  const encode = (config: Config) => {
    const pairs: number[] = [];
    for (const [key, value] of Object.entries(config)) {
      let k = keys.get(key);
      if (k === undefined) { k = result.keys.push(key) - 1; keys.set(key, k); }
      const text = JSON.stringify(value);
      let v = values.get(text);
      if (v === undefined) { v = result.values.push(value) - 1; values.set(text, v); }
      pairs.push(k, v);
    }
    return pairs;
  };
  for (const kind of ['machines', 'processes', 'filaments'] as const) {
    const groups = new Map<string, SystemPreset[]>();
    for (const preset of catalog[kind]) {
      const keys = JSON.stringify(Object.keys(preset.config).sort());
      const group = groups.get(keys) ?? [];
      group.push(preset);
      groups.set(keys, group);
    }
    const packed = new Map<SystemPreset, CompactPreset>();
    for (const group of groups.values()) {
      const base: Config = {};
      for (const key of Object.keys(group[0].config)) {
        const counts = new Map<string, number>();
        for (const p of group) {
          const text = JSON.stringify(p.config[key]);
          counts.set(text, (counts.get(text) ?? 0) + 1);
        }
        const most = [...counts].sort((a, b) => b[1] - a[1])[0][0];
        base[key] = JSON.parse(most);
      }
      const index = result.bases.push(encode(base)) - 1;
      for (const p of group) {
        const config = Object.fromEntries(Object.entries(p.config).filter(([key, value]) =>
          JSON.stringify(value) !== JSON.stringify(base[key])));
        packed.set(p, { ...p, config: encode(config), base: index });
      }
    }
    result[kind] = catalog[kind].map((p) => packed.get(p)!);
  }
  return result;
}

export function expandCatalog(raw: CompactCatalog, normalize: (config: Config) => Config = (config) => config): PresetCatalog {
  const catalog = {} as PresetCatalog;
  const decode = (pairs: number[], config: Config) => {
    for (let i = 0; i < pairs.length; i += 2) config[raw.keys[pairs[i]]] = raw.values[pairs[i + 1]];
    return config;
  };
  for (const kind of ['machines', 'processes', 'filaments'] as const) {
    catalog[kind] = raw[kind].map(({ config, base, ...preset }) => {
      let resolved: Config | undefined;
      return { ...preset, get config() { return resolved ??= normalize(decode(config, decode(raw.bases[base], {}))); } };
    });
  }
  return catalog;
}
