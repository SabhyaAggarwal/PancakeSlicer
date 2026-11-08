import { execFileSync } from 'node:child_process';
import { mkdirSync, rmSync } from 'node:fs';
import path from 'node:path';
import { roofPair } from './roof-shots.ts';

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

await page.evaluate(() => (window as any).__plater.setState({ tab: 'prepare', presetUx: { kind: 'presets', type: 'process', name: 'profile:starter-everyday', edit: true } }));
await page.getByRole('tab', { name: /Dynamic$/ }).first().click({ timeout: 30_000 });
await page.locator('.dyn-formulas').first().waitFor();
await page.addStyleTag({ content: '.dyn-rows > :not(.dyn-formulas), .dyn-rows > .dyn-printer{display:none!important} .dyn-rows{width:max-content!important;grid-template-columns:max-content max-content max-content max-content max-content!important}' });
await page.waitForTimeout(600);
const dyn = (await page.locator('.dyn-formulas').first().boundingBox())!;
await save('dynamic', (file) => page.screenshot({ path: file, clip: { x: dyn.x - 12, y: dyn.y - 12, width: dyn.width + 24, height: dyn.height + 24 } }));
await page.keyboard.press('Escape');
await page.reload({ waitUntil: 'load' });
await ready();

await page.getByRole('button', { name: 'Projects', exact: true }).first().click();
await page.getByRole('button', { name: 'New project', exact: true }).first().click();
await page.getByRole('button', { name: 'Benchy', exact: true }).click({ timeout: 30_000 });
await page.waitForFunction(() => (window as any).__plater.getState().objects.length === 1 && !(window as any).__plater.getState().loading, null, { timeout: 60_000 });
const facts = await roofPair(page, 'roof', save);
await page.reload({ waitUntil: 'load' });
await ready();

for (const example of ['Knob', 'Shelf hook']) {
  await page.getByRole('button', { name: 'Projects', exact: true }).first().click();
  await page.getByRole('button', { name: 'New project', exact: true }).first().click();
  await page.getByRole('button', { name: example, exact: true }).click({ timeout: 30_000 });
  await page.waitForFunction(() => (window as any).__plater.getState().objects.length === 1 && !(window as any).__plater.getState().loading, null, { timeout: 60_000 });
  await page.waitForTimeout(2500);
}
await context.route(/\/api\/project-leases\/[^/]+$/, (route) => route.fulfill({ status: 409, json: { code: 'lease_held', holder: { label: 'Safari on iPad', since: Date.now(), lastSeen: Date.now() } } }));
await page.setViewportSize({ width: 1200, height: 540 });
await page.reload({ waitUntil: 'load' });
await ready();
await page.locator('.lease-bar').waitFor({ timeout: 30_000 });
await page.getByRole('button', { name: 'Projects', exact: true }).first().click();
await page.waitForTimeout(2000);
await save('workflow', (file) => page.screenshot({ path: file }));
await context.unroute(/\/api\/project-leases\/[^/]+$/);

rmSync(tmp, { recursive: true, force: true });
await browser.close();
console.log(JSON.stringify(facts));
