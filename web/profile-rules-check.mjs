import { createServer } from 'vite';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import path from 'node:path';

const root = path.dirname(fileURLToPath(import.meta.url));
const spec = JSON.parse(readFileSync(path.join(root, '../server/profile_spec.json'), 'utf8'));
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), appType: 'custom', logLevel: 'silent',
  cacheDir: path.join(tmpdir(), 'orca-profile-rules-check'), server: { middlewareMode: true, hmr: false, watch: null } });
let failed = 0, checked = 0;
const expect = (what, got, want) => { checked++; if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.error(`FAIL ${what}\n  got      ${JSON.stringify(got)}\n  expected ${JSON.stringify(want)}`); } };
try {
  const { DEFAULT_SCALES, clampHeight, heightsFor, scaleValues } = await server.ssrLoadModule('/src/profiles/auto-rules.ts');
  for (const c of spec.scales) {
    expect(`clamp ${c.nozzle}/${c.layerHeight}`, clampHeight(c.layerHeight, c.nozzle), c.clamped);
    expect(`default scales ${c.nozzle}/${c.layerHeight}`, scaleValues(DEFAULT_SCALES, c), c.values);
  }
  for (const c of spec.scaleRows) expect(`rows ${c.nozzle}/${c.layerHeight}`, scaleValues(c.rows, c), c.values);
  for (const c of spec.heights) expect(`heights ${c.nozzle} ${c.min}-${c.max}`, heightsFor(c.nozzle, c.min, c.max), c.heights);
  console.log(failed ? `${failed} of ${checked} failed` : `profile spec: ${checked} passed`);
} catch (error) { failed++; console.error(error); }
process.exit(failed ? 1 : 0);
