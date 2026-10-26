import { COEF, INTERCEPT, RECIPES } from './mixerModel';

const POWERS: number[][] = [];
for (let d = 0; d <= 4; d++) {
  const rec = (start: number, left: number, p: number[]) => {
    if (!left) return void POWERS.push(p);
    for (let i = start; i < 7; i++) rec(i, left - 1, p.map((v, k) => (k === i ? v + 1 : v)));
  };
  rec(0, d, [0, 0, 0, 0, 0, 0, 0]);
}

const rgb = (hex: string) => (/^#[0-9a-f]{6}/i.test(hex) ? [1, 3, 5].map((k) => parseInt(hex.slice(k, k + 2), 16)) : [128, 128, 128]);
const hexOf = (c: number[]) => `#${c.map((v) => v.toString(16).padStart(2, '0')).join('')}`.toUpperCase();

function lerp(a: number[], b: number[], t: number): number[] {
  if (t <= 0) return a;
  if (t >= 1) return b;
  const x = [...a, ...b, t];
  return [0, 1, 2].map((c) => {
    let sum = INTERCEPT[c];
    POWERS.forEach((p, i) => { let f = 1; p.forEach((e, j) => { for (let k = 0; k < e; k++) f *= x[j]; }); sum += f * COEF[i * 3 + c]; });
    return Math.min(255, Math.max(0, Math.trunc(sum)));
  });
}

function labHex([l, a, b]: number[]): string {
  const inv = (t: number) => (t ** 3 > 216 / 24389 ? t ** 3 : (t * 116 - 16) / (24389 / 27));
  const fy = (l + 16) / 116, X = 0.95047 * inv(a / 500 + fy), Y = inv(fy), Z = 1.08883 * inv(fy - b / 200);
  const g = (c: number) => { c = Math.max(0, Math.min(1, c)); return c <= 0.0031308 ? 12.92 * c : 1.055 * c ** (1 / 2.4) - 0.055; };
  return hexOf([3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.204 * Y + 1.057 * Z]
    .map((c) => Math.max(0, Math.min(255, Math.round(g(c) * 255)))));
}

function measured(hexes: string[], weights: number[]): string | null {
  const n = hexes.length;
  const pairs = hexes.map((h, i) => [hexOf(rgb(h)), weights[i]] as [string, number]).sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : a[1] - b[1]));
  const keys = pairs.map((p) => p[0]);
  let ratios = pairs.map((p) => p[1]);
  const total = ratios.reduce((a, b) => a + b, 0);
  if (total > 0 && total !== 100) {
    ratios = ratios.map((r) => Math.round((r * 100) / total));
    const k = ratios.indexOf(Math.max(...ratios));
    ratios[k] += 100 - ratios.reduce((a, b) => a + b, 0);
  }
  if (ratios.some((r) => r < 20 || (n === 2 && r > 80))) return null;
  const anchors: { ratios: number[]; lab: number[]; hex: string }[] = [];
  for (const [comps, rs, lab, hex] of RECIPES) {
    if (comps.length !== n) continue;
    const e = comps.map((h, i) => [hexOf(rgb(h)), rs[i]] as [string, number]).sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : a[1] - b[1]));
    if (e.some((p, i) => p[0] !== keys[i])) continue;
    const a = { ratios: e.map((p) => p[1]), lab, hex: hexOf(rgb(hex)) };
    if (a.ratios.every((r, i) => r === ratios[i])) return a.hex;
    anchors.push(a);
  }
  if (anchors.length < 2) return null;
  if (n === 2) {
    anchors.sort((a, b) => a.ratios[0] - b.ratios[0]);
    let lo = 0;
    while (lo + 2 < anchors.length && anchors[lo + 1].ratios[0] <= ratios[0]) lo++;
    const [a0, a1] = [anchors[lo], anchors[lo + 1]], span = a1.ratios[0] - a0.ratios[0];
    const t = span > 0 ? (ratios[0] - a0.ratios[0]) / span : 0;
    return labHex(a0.lab.map((v, i) => v + t * (a1.lab[i] - v)));
  }
  const near = anchors.map((a) => ({ a, d: Math.hypot(ratios[0] - a.ratios[0], ratios[1] - a.ratios[1]) })).sort((x, y) => x.d - y.d);
  if (near[0].d === 0) return near[0].a.hex;
  const w = near.slice(0, 3).map(({ a, d }) => ({ a, w: 1 / (d * d) })), den = w.reduce((s, x) => s + x.w, 0);
  return labHex([0, 1, 2].map((c) => w.reduce((s, x) => s + x.w * x.a.lab[c], 0) / den));
}

export function blend(hexes: string[], weights: number[]): string {
  const m = hexes.length >= 2 ? measured(hexes, weights) : null;
  if (m) return m;
  let c = [128, 128, 128], acc = 0;
  hexes.forEach((h, i) => {
    if (weights[i] <= 0) return;
    if (!acc) { c = rgb(h); acc = weights[i]; return; }
    const total = acc + weights[i];
    c = lerp(c, rgb(h), Math.fround(weights[i] / total));
    acc = total;
  });
  return acc ? hexOf(c) : '#000000';
}
