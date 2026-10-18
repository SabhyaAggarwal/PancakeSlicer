import { readFileSync } from 'node:fs';
import type { Plugin } from 'vite';

export function optionAssets(): Plugin {
  return {
    name: 'orca-option-bootstrap',
    resolveId(id) {
      if (id === 'virtual:option-bootstrap') return '\0' + id;
    },
    load(id) {
      if (id !== '\0virtual:option-bootstrap') return;
      const read = (path: string) => {
        const file = new URL(path, import.meta.url);
        this.addWatchFile(file.pathname);
        return JSON.parse(readFileSync(file, 'utf8'));
      };
      const options = read('./src/options.generated.json');
      const quick = Object.fromEntries(['curr_bed_type', 'flush_multiplier'].map((key) => [key, options[key]]));
      const numeric = Object.keys(options).filter((key) => /^(Floats|Ints|Percents|Bools|FloatsOrPercents|Points)$/.test(options[key].type));
      const removed = [...new Set(['process', 'printer', 'filament'].flatMap((tab) => read(`./src/tabs/${tab}.json`).removed_keys))];
      return `export const quick = ${JSON.stringify(quick)}; export const numeric = ${JSON.stringify(numeric)}; export const removed = ${JSON.stringify(removed)};`;
    },
  };
}
