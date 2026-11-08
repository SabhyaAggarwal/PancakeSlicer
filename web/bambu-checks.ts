import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { Plugin } from 'vite';

export function bambuChecks(): Plugin {
  const resources = fileURLToPath(new URL('../engine/resources/', import.meta.url));
  const read = (relative: string) => JSON.parse(fs.readFileSync(path.join(resources, relative), 'utf8'));
  return {
    name: 'bambu-checks',
    resolveId(id) { if (id === 'virtual:bambu-checks') return '\0' + id; },
    load(id) {
      if (id !== '\0virtual:bambu-checks') return;
      const index = read('profiles/BBL.json');
      const modelIds = Object.fromEntries(index.machine_model_list.map((row: { name: string; sub_path: string }) => [row.name, read(`profiles/BBL/${row.sub_path}`).model_id]));
      const filamentIds = Object.fromEntries(Object.entries(read('printers/bambu_filament_ids.json').filaments).map(([id, row]) => [id, (row as { bambu_id: string }).bambu_id]));
      return `export const rules=${JSON.stringify(read('printers/filaments_blacklist.json').blacklist)};\nexport const hardness=${JSON.stringify(read('info/nozzle_info.json').nozzle_hrc)};\nexport const modelIds=${JSON.stringify(modelIds)};\nexport const filamentIds=${JSON.stringify(filamentIds)};`;
    },
  };
}
