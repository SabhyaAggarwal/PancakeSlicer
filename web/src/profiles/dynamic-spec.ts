import { readFileSync } from 'node:fs';
import type { Config } from '../api/types';
import { availableHeights, dynamicConflicts, resolveDynamic, type DynamicRecord } from './dynamic.ts';
import { matchesFilament, matchesPrint, matchesPrinter, validWorksWith } from './works-with.ts';

const spec = JSON.parse(readFileSync(new URL('../../../server/profile_dynamic_spec.json', import.meta.url), 'utf8'));
const record = spec.dynamic as DynamicRecord & { below: Config };
const same = (a: unknown, b: unknown) => JSON.stringify(a, Object.keys(a as object).sort()) === JSON.stringify(b, Object.keys(b as object).sort());
let failed = 0;
const check = (ok: boolean, what: string, got: unknown) => { if (!ok) { failed++; console.error('FAIL', what, JSON.stringify(got)); } };
for (const c of spec.resolve) {
  const got = resolveDynamic(record, c.ctx, record.below);
  check(same(got, c.config), `resolve ${JSON.stringify(c.ctx)}`, got);
  const conflicts = dynamicConflicts(record, c.ctx);
  check(JSON.stringify(conflicts) === JSON.stringify(c.conflicts), `conflicts ${JSON.stringify(c.ctx)}`, conflicts);
}
for (const c of spec.heights) {
  const got = availableHeights(record, c.ctx);
  check(JSON.stringify(got) === JSON.stringify(c.heights), `heights ${JSON.stringify(c.ctx)}`, got);
}
for (const c of spec.rules) {
  for (const p of c.printers ?? []) check(matchesPrinter(c.rule, p) === p.match, `printer ${JSON.stringify(c.rule)} ${JSON.stringify(p)}`, !p.match);
  for (const f of c.filaments ?? []) check(matchesFilament(c.rule, f) === f.match, `filament ${JSON.stringify(f)}`, !f.match);
  for (const p of c.prints ?? []) check(matchesPrint(c.rule, p.id) === p.match, `print ${JSON.stringify(p)}`, !p.match);
}
for (const c of spec.valid) check(validWorksWith(c.rule, c.type) === c.valid, `valid ${c.type} ${JSON.stringify(c.rule)}`, !c.valid);
console.log(failed ? `${failed} failed` : 'profile_dynamic_spec: all passed');
process.exitCode = failed ? 1 : 0;
