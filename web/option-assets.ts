

import { readFileSync } from 'node:fs';
import type { Plugin } from 'vite';

export function optionAssets(bootstrapTypes = true): Plugin {
  return {
    name: 'orca-option-bootstrap',
    resolveId(id) {
      if (id === 'virtual:option-bootstrap' || id === 'virtual:option-hints') return '\0' + id;
    },
    load(id) {
      if (id !== '\0virtual:option-bootstrap' && id !== '\0virtual:option-hints') return;
      const read = (path: string) => {
        const file = new URL(path, import.meta.url);
        this.addWatchFile(file.pathname);
        return JSON.parse(readFileSync(file, 'utf8'));
      };
      const options = read('./src/options.generated.json');
      if (id === '\0virtual:option-hints') {

        const names = ['gui_type', 'multiline', 'full_width'];
        const hints = Object.fromEntries(Object.entries<Record<string, unknown>>(options)
          .filter(([, def]) => names.some((name) => def[name] !== undefined))
          .map(([key, def]) => [key, Object.fromEntries(['type', ...names].filter((name) => def[name] !== undefined).map((name) => [name, def[name]]))]));
        return `export const hints = ${JSON.stringify(hints)};`;
      }
      const quick = Object.fromEntries(['curr_bed_type', 'flush_multiplier'].map((key) => [key, options[key]]));
      const vectors = Object.fromEntries((bootstrapTypes ? Object.keys(options) : [])
        .filter((key) => /^(Floats|Ints|Percents|Bools|FloatsOrPercents|Points|Enums|Strings|PointsGroups|String|Point|Point3)$/.test(options[key].type))
        .map((key) => [key, options[key].type]));
      const groups = new Map<string, string[]>();
      for (const [key, type] of Object.entries(vectors)) {
        const keys = groups.get(type) ?? [];
        keys.push(key);
        groups.set(type, keys);
      }
      const grouped = [...groups].map(([type, keys]) => [type, keys.join(' ')]);
      const removed = [...new Set(['process', 'printer', 'filament'].flatMap((tab) => read(`./src/tabs/${tab}.json`).removed_keys))];
      return `export const quick = ${JSON.stringify(quick)}; export const vectors = Object.fromEntries(${JSON.stringify(grouped)}.flatMap(([type, keys]) => keys.split(' ').map(key => [key, type]))); export const removed = ${JSON.stringify(removed)};`;
    },
  };
}
