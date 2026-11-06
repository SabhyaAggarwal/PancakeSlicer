
import { readFileSync } from 'node:fs';
import { gunzipSync, gzipSync } from 'node:zlib';
import type { Plugin } from 'vite';
import { compactCatalog } from './src/api/compact-catalog.ts';

export function mockAssets(): Plugin {
  const source = new URL('./src/mock/presets.json.gz', import.meta.url);
  const id = '\0virtual:mock-presets';
  const route = '/@orca/mock-presets.json.gz';
  let bytes: Buffer;
  function compact() {
    const catalog = JSON.parse(gunzipSync(readFileSync(source)).toString());
    return gzipSync(JSON.stringify(compactCatalog(catalog)), { level: 9 });
  }
  return {
    name: 'orca-mock-catalog',
    resolveId(name) { if (name === 'virtual:mock-presets') return id; },
    load(name) {
      if (name !== id) return;
      this.addWatchFile(source.pathname);
      bytes = compact();
      if (this.environment.mode === 'dev') return `export default ${JSON.stringify(route)}`;
      const ref = this.emitFile({ type: 'asset', name: 'mock-presets.json.gz', source: bytes });
      return `export default import.meta.ROLLUP_FILE_URL_${ref}`;
    },
    configureServer(server) {
      server.middlewares.use(route, (_req, res) => {
        res.setHeader('Content-Type', 'application/gzip');
        res.end(compact());
      });
    },
  };
}
