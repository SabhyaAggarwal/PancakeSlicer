import type { Page } from 'playwright-core';

const ROOF: [[number, number, number], [number, number, number]] = [[84, 80, 35], [102, 100, 41]];
const CROP: [[number, number, number], [number, number, number]] = [[84, 80, 35], [102, 100, 48]];

export interface RoofFacts { time?: string | null; curved?: string | null }

export async function roofPair(page: Page, prefix: string, save: (name: string, shot: (file: string) => Promise<unknown>) => Promise<void>) {
  const facts: Record<string, RoofFacts> = {};
  for (const [name, mode] of [['flat', 'off'], ['curved', 'curved_tops']] as const) {
    await page.evaluate((m) => { const p = (window as any).__plater; p.setState((s: any) => ({ tab: 'prepare', dirty: { ...s.dirty, process: { ...s.dirty.process, nonplanar_mode: m } } })); }, mode);
    const before = await page.evaluate(() => (window as any).__plater.getState().job.id ?? null);
    await page.getByRole('button', { name: /^Slice/ }).first().click();
    await page.waitForFunction((id) => { const j = (window as any).__plater.getState().job; return j.id !== id && ['done', 'failed'].includes(j.state); }, before, { timeout: 600_000, polling: 500 });
    const job = await page.evaluate(() => { const j = (window as any).__plater.getState().job; return { state: j.state, error: String(j.error ?? '') }; });
    if (job.state !== 'done') throw new Error(`roof ${name}: slice failed: ${job.error}`);
    await page.evaluate(() => (window as any).__plater.setState({ tab: 'preview' }));
    await page.waitForFunction(() => (window as any).__preview?.data?.done, null, { timeout: 120_000, polling: 300 });
    await page.mouse.move(5, 5);
    await page.waitForTimeout(1000);
    facts[name] = await page.evaluate(() => ({ time: document.querySelector('.lg-stat-v')?.textContent, curved: document.querySelector('.np-summary-line')?.textContent ?? null }));
    if (mode === 'curved_tops' && !/[1-9][\d.]* % of top surface curved/.test(facts[name].curved ?? '')) throw new Error(`roof curved: no curved tops (${facts[name].curved})`);
    await page.addStyleTag({ content: '.legend,.layer-slider,.order-bar,.nav3d,.notifications,.toast,.preview-status,.tool-position,.gcode-window,.canvas-tools{visibility:hidden!important}' });
    await page.evaluate((b) => { const s = (window as any).__preview.scene, V = s.camera.position.constructor; s.fit(new V(...b[0]), new V(...b[1])); }, ROOF);
    await page.waitForTimeout(1500);
    const r = await page.evaluate((b) => {
      const s = (window as any).__preview.scene, V = s.camera.position.constructor;
      const el = document.querySelector('.preview-canvas canvas')!.getBoundingClientRect();
      let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
      for (const x of [b[0][0], b[1][0]]) for (const y of [b[0][1], b[1][1]]) for (const z of [b[0][2], b[1][2]]) {
        const p = new V(x, y, z).project(s.camera), sx = el.left + (p.x + 1) / 2 * el.width, sy = el.top + (1 - p.y) / 2 * el.height;
        x0 = Math.min(x0, sx); x1 = Math.max(x1, sx); y0 = Math.min(y0, sy); y1 = Math.max(y1, sy);
      }
      return { x0, y0, x1, y1, left: el.left, top: el.top, right: el.right, bottom: el.bottom };
    }, CROP);
    const side = Math.min(Math.max(r.x1 - r.x0, r.y1 - r.y0) * 1.08, r.right - r.left, r.bottom - r.top);
    const x = Math.min(Math.max((r.x0 + r.x1 - side) / 2, r.left), r.right - side), y = Math.min(Math.max((r.y0 + r.y1 - side) / 2, r.top), r.bottom - side);
    await save(`${prefix}-${name}`, (file) => page.screenshot({ path: file, clip: { x, y, width: side, height: side } }));
  }
  return facts;
}
