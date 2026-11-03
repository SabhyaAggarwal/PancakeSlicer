import { createServer } from 'vite';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import path from 'node:path';

const root = path.dirname(fileURLToPath(import.meta.url));
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), appType: 'custom', logLevel: 'silent',
  cacheDir: path.join(tmpdir(), 'orca-profile-rules-check'), server: { middlewareMode: true, hmr: false, watch: null } });
let failed = 0;
try {
  const { worksWithTable } = await server.ssrLoadModule('/src/profiles/works-with-table.ts');
  for (const [what, got, expected] of worksWithTable()) {
    if (JSON.stringify(got) === JSON.stringify(expected)) continue;
    failed++;
    console.error(`FAIL ${what}\n  got      ${JSON.stringify(got)}\n  expected ${JSON.stringify(expected)}`);
  }
  console.log(failed ? `${failed} failed` : 'works with table: all passed');
} catch (error) { failed++; console.error(error); }
process.exit(failed ? 1 : 0);
