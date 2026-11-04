import { execFileSync } from 'node:child_process';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const [base = 'http://127.0.0.1:20220/', out = 'public/tour'] = process.argv.slice(2);
const { chromium } = await import(process.env.PLAYWRIGHT_CORE ?? 'playwright-core');
const browser = await chromium.launch({ executablePath: process.env.CHROME, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 2 });
const page = await context.newPage();
mkdirSync(out, { recursive: true });
const tmp = path.join(out, '.png');
mkdirSync(tmp, { recursive: true });
async function save(name: string, shot: (file: string) => Promise<unknown>) {
  const png = path.join(tmp, `${name}.png`);
  await shot(png);
  execFileSync('cwebp', ['-quiet', '-q', '80', png, '-o', path.join(out, `${name}.webp`)]);
}
const state = <T>(f: string) => page.evaluate(f) as Promise<T>;
const ready = () => page.waitForFunction(() => (window as any).__plater?.getState().profileLibraryLoad?.state === 'ready', null, { timeout: 60_000 });

await page.goto(base, { waitUntil: 'load' });
await ready();
await page.waitForTimeout(2000);
if (!await page.locator('.setup').count()) {
  await page.getByRole('button', { name: 'Application menu' }).click();
  await page.getByRole('menuitem', { name: 'Setup', exact: true }).click();
}
await page.getByRole('button', { name: 'Skip to setup' }).click();
await page.getByRole('tab', { name: 'Bambu Lab' }).click();
await page.locator('.setup-model', { hasText: /A1 mini$/ }).first().click();
await page.getByRole('button', { name: 'Next', exact: true }).click();
await page.waitForFunction(() => document.querySelector('.setup-body h2')?.textContent === 'Print profiles', null, { timeout: 60_000 });
await page.getByRole('button', { name: 'Next', exact: true }).click();
await page.getByRole('button', { name: 'Skip', exact: true }).click();
await page.getByRole('button', { name: 'Done', exact: true }).click();
await page.getByRole('button', { name: 'Start printing' }).click({ timeout: 120_000 });
await page.waitForTimeout(1500);

await page.getByRole('button', { name: 'Knob', exact: true }).click();
await page.waitForFunction(() => (window as any).__plater.getState().objects.length === 1 && !(window as any).__plater.getState().loading, null, { timeout: 60_000 });
const facts: Record<string, unknown> = {};
for (const [name, mode] of [['curved-flat', 'off'], ['curved-tops', 'curved_tops']] as const) {
  await page.evaluate((m) => { const p = (window as any).__plater; p.setState((s: any) => ({ tab: 'prepare', dirty: { ...s.dirty, process: { ...s.dirty.process, nonplanar_mode: m } } })); }, mode);
  const before = await state<string | null>('window.__plater.getState().job.id ?? null');
  await page.getByRole('button', { name: /^Slice/ }).first().click();
  await page.waitForFunction((id) => { const j = (window as any).__plater.getState().job; return j.id !== id && ['done', 'failed'].includes(j.state); }, before, { timeout: 300_000, polling: 500 });
  if (await state<string>('window.__plater.getState().job.state') !== 'done') throw new Error(`${name}: slice failed: ${await state('window.__plater.getState().job.error')}`);
  await page.evaluate(() => (window as any).__plater.setState({ tab: 'preview' }));
  await page.waitForFunction(() => (window as any).__preview?.data?.done, null, { timeout: 120_000, polling: 300 });
  await page.evaluate(() => window.dispatchEvent(new CustomEvent('camera-fit')));
  await page.mouse.move(5, 5);
  await page.waitForTimeout(1500);
  facts[name] = await page.evaluate(() => ({ time: document.querySelector('.lg-stat-v')?.textContent, curved: document.querySelector('.np-summary-line')?.textContent ?? null }));
  await page.addStyleTag({ content: '.legend,.layer-slider,.order-bar,.nav3d,.notifications,.toast,.preview-status,.tool-position,.gcode-window,.canvas-tools{visibility:hidden!important}' });
  await page.waitForTimeout(300);
  const canvas = page.locator('.preview-canvas canvas').first();
  const box = (await canvas.boundingBox())!;
  const side = Math.min(box.width, box.height) * 0.7;
  await save(name, (file) => page.screenshot({ path: file, clip: { x: box.x + (box.width - side) / 2, y: box.y + (box.height - side) / 2, width: side, height: side } }));
}

await page.evaluate(() => (window as any).__plater.setState({ tab: 'prepare', presetUx: { kind: 'presets', type: 'process', name: 'profile:starter-everyday', edit: true } }));
await page.getByRole('tab', { name: /Dynamic$/ }).first().click({ timeout: 30_000 });
await page.locator('.dyn-tab').first().waitFor();
await page.waitForTimeout(600);
const dyn = (await page.locator('.dyn-tab').first().boundingBox())!;
await save('dynamic', (file) => page.screenshot({ path: file, clip: { x: dyn.x, y: dyn.y, width: dyn.width, height: Math.min(dyn.height, 700) } }));

for (const tab of await page.getByRole('tab').all()) {
  if (/Dynamic/.test(await tab.innerText())) continue;
  await tab.click();
  if (await page.locator('.modal .source-glyph:visible').count()) break;
}
const glyph = page.locator('.modal .source-glyph:visible').first();
await glyph.scrollIntoViewIfNeeded();
await glyph.hover();
await page.waitForTimeout(800);
const row = (await glyph.evaluateHandle((el) => { let n: Element | null = el; while (n && n.getBoundingClientRect().width < 240) n = n.parentElement; return n ?? el; })).asElement()!;
const r = (await row.boundingBox())!;
await save('printer-values', (file) => page.screenshot({ path: file, clip: { x: Math.max(0, r.x - 12), y: Math.max(0, r.y - 12), width: Math.min(560, r.width + 24), height: r.height + 72 } }));
await page.keyboard.press('Escape');

await context.route(/\/api\/project-leases\/[^/]+$/, (route) => route.fulfill({ status: 409, json: { code: 'lease_held', holder: { label: 'Safari on iPad', since: Date.now(), lastSeen: Date.now() } } }));
await page.reload({ waitUntil: 'load' });
await ready();
await page.locator('.lease-bar').waitFor({ timeout: 30_000 });
await save('take-control', (file) => page.locator('.lease-bar').screenshot({ path: file }));
await context.unroute(/\/api\/project-leases\/[^/]+$/);
await page.getByRole('button', { name: 'Projects', exact: true }).first().click();
await page.waitForTimeout(1500);
await save('projects', (file) => page.screenshot({ path: file, clip: { x: 0, y: 40, width: 1100, height: 560 } }));

writeFileSync(path.join(out, 'tour.json'), `${JSON.stringify(facts, null, 2)}\n`);
rmSync(tmp, { recursive: true, force: true });
await browser.close();
console.log(JSON.stringify(facts));
