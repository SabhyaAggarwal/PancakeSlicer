



import standard from './flush/flush_data_standard.txt?raw';
import dualStandard from './flush/flush_data_dual_standard.txt?raw';
import dualHighflow from './flush/flush_data_dual_highflow.txt?raw';
import type { Config, ConfigValue } from './api/types';

const f = Math.fround;
export const MIN_FLUSH_VOLUME_FROM_SUPPORT = 700;
export const FLUSH_VOLUME_TO_SUPPORT = 230;
export const MAX_FLUSH_VOLUME = 20000;

export const MULTIPLIER_RANGE: [number, number] = [0, 3];

type RGB = [number, number, number];

export function parseColour(hex: string): { rgb: RGB; alpha: number } {
  const m = /^#?([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(hex.trim());
  if (!m) return { rgb: [0, 0, 0], alpha: 255 };
  const n = parseInt(m[1], 16);
  return { rgb: [(n >> 16) & 255, (n >> 8) & 255, n & 255], alpha: m[2] ? parseInt(m[2], 16) : 255 };
}



function rgbToLab([r8, g8, b8]: RGB): [number, number, number] {
  const gamma = (x: number) => (x > 0.04045 ? Math.pow((x + 0.055) / 1.055, 2.4) : x / 12.92);
  const R = gamma(r8 / 255) * 100, G = gamma(g8 / 255) * 100, B = gamma(b8 / 255) * 100;
  const x = 0.412453 * R + 0.35758 * G + 0.180423 * B;
  const y = 0.212671 * R + 0.71516 * G + 0.072169 * B;
  const z = 0.019334 * R + 0.119193 * G + 0.950227 * B;
  const threshold = f(0.008856);
  const ff = (t: number) => (t > threshold ? Math.pow(t, 1 / 3) : 7.787 * t + 0.137931);
  const xn = ff(x / 95.0489), yn = ff(y / 100), zn = ff(z / 108.884);
  return [116 * yn - 16, 500 * (xn - yn), 200 * (yn - zn)];
}

function deltaE00(c1: RGB, c2: RGB): number {
  const [l1, a1, b1] = rgbToLab(c1), [l2, a2, b2] = rgbToLab(c2);
  const rad = (d: number) => (d * Math.PI) / 180;
  const p25 = Math.pow(25, 7);
  const C1 = Math.hypot(a1, b1), C2 = Math.hypot(a2, b2);
  const cm7 = Math.pow((C1 + C2) / 2, 7);
  const G = 0.5 * (1 - Math.sqrt(cm7 / (cm7 + p25)));
  const pa1 = (1 + G) * a1, pa2 = (1 + G) * a2;
  const pc1 = Math.hypot(pa1, b1), pc2 = Math.hypot(pa2, b2);
  const hue = (a: number, b: number) => (a === 0 && b === 0 ? 0 : (Math.atan2(b, a) + 2 * Math.PI) % (2 * Math.PI));
  const ph1 = hue(pa1, b1), ph2 = hue(pa2, b2);
  const dL = l2 - l1, dC = pc2 - pc1;
  let dH = 0;
  if (pc1 * pc2 !== 0) {
    dH = ph2 - ph1;
    if (dH < -Math.PI) dH += 2 * Math.PI;
    else if (dH > Math.PI) dH -= 2 * Math.PI;
    dH = 2 * Math.sqrt(pc1 * pc2) * Math.sin(dH / 2);
  }
  const Lm = (l1 + l2) / 2, Cm = (pc1 + pc2) / 2;
  let Hm = ph1 + ph2;
  if (pc1 * pc2 !== 0) {
    if (Math.abs(ph1 - ph2) <= Math.PI) Hm /= 2;
    else Hm = Hm < 2 * Math.PI ? (Hm + 2 * Math.PI) / 2 : (Hm - 2 * Math.PI) / 2;
  }
  const T = 1 - 0.17 * Math.cos(Hm - rad(30)) + 0.24 * Math.cos(2 * Hm) + 0.32 * Math.cos(3 * Hm + rad(6)) - 0.2 * Math.cos(4 * Hm - rad(63));
  const dTheta = rad(30) * Math.exp(-Math.pow((Hm - rad(275)) / rad(25), 2));
  const RC = 2 * Math.sqrt(Math.pow(Cm, 7) / (Math.pow(Cm, 7) + p25));
  const SL = 1 + (0.015 * Math.pow(Lm - 50, 2)) / Math.sqrt(20 + Math.pow(Lm - 50, 2));
  const SC = 1 + 0.045 * Cm, SH = 1 + 0.015 * Cm * T;
  const RT = -Math.sin(2 * dTheta) * RC;
  return f(Math.sqrt((dL / SL) ** 2 + (dC / SC) ** 2 + (dH / SH) ** 2 + RT * (dC / SC) * (dH / SH)));
}

class Predictor {
  colors: RGB[] = [];
  table = new Map<string, number>();
  valid = false;
  constructor(text: string) {
    const lines = text.split(/\r?\n/);
    const hex = (s: string) => (/^#[0-9a-fA-F]{6}$/.test(s) ? parseColour(s).rgb : null);
    const colors = (lines[1] ?? '').trim().split(/\s+/).map(hex);
    if (colors.some((c) => !c)) return;
    this.colors = colors as RGB[];
    for (const line of lines.slice(3)) {
      if (!line.trim()) continue;
      const [a, b, v] = line.trim().split(/\s+/);
      if (!hex(a) || !hex(b) || !Number.isFinite(Number(v))) return;
      this.table.set(`${a.toUpperCase()}${b.toUpperCase()}`, f(Number(v)));
    }
    this.valid = true;
  }
  predict(from: RGB, to: RGB): number | null {
    if (!this.valid) return null;
    const similar = (c: RGB) => this.colors.find((x) => deltaE00(x, c) <= 5);
    const a = similar(from), b = similar(to);
    if (!a || !b) return null;
    const key = (c: RGB) => '#' + c.map((v) => v.toString(16).padStart(2, '0')).join('').toUpperCase();
    return this.table.get(key(a) + key(b)) ?? null;
  }
}

const predictors: Record<number, Predictor> = {};
function predictor(dataset: number): Predictor {
  const text = dataset === 0 ? standard : dataset === 1 ? dualStandard : dataset === 2 ? dualHighflow : '';
  return (predictors[dataset] ??= new Predictor(text));
}



function rgbToHsv(r: number, g: number, b: number): [number, number, number] {
  const cmax = Math.max(r, g, b), cmin = Math.min(r, g, b), delta = f(cmax - cmin);
  let h: number;
  if (Math.abs(delta) < 0.001) h = 0;
  else if (cmax === r) h = f(60 * f(f((g - b) / delta) % 6));
  else if (cmax === g) h = f(60 * f((b - r) / delta + 2));
  else h = f(60 * f((r - g) / delta + 4));
  const s = Math.abs(cmax) < 0.001 ? 0 : f(delta / cmax);
  return [h, s, cmax];
}

const luminance = (r: number, g: number, b: number) => f(r * 0.3 + g * 0.59 + b * 0.11);
const toRad = (deg: number) => f((deg / 180) * f(Math.PI));

function deltaHS(h1: number, s1: number, v1: number, h2: number, s2: number, v2: number) {
  const a = toRad(h1), b = toRad(h2);
  const dx = f(f(Math.cos(a)) * s1 * v1 - Math.cos(b) * s2 * v2);
  const dy = f(f(Math.sin(a)) * s1 * v1 - Math.sin(b) * s2 * v2);
  return Math.min(1.2, f(Math.sqrt(dx * dx + dy * dy)));
}

function flushRgb(src: RGB, dst: RGB, dataset: number): number {
  if (dataset === 0) {
    const p = predictor(0).predict(src, dst);
    if (p !== null) return Math.trunc(p);
  }
  const [sr, sg, sb] = src.map((v) => f(v / 255));
  const [dr, dg, db] = dst.map((v) => f(v / 255));
  const [fh, fs, fv] = rgbToHsv(sr, sg, sb);
  const [th, ts, tv] = rgbToHsv(dr, dg, db);
  let hsDist = deltaHS(fh, fs, fv, th, ts, tv);
  const fromLumi = luminance(sr, sg, sb), toLumi = luminance(dr, dg, db);
  let lumiFlush: number;
  if (toLumi >= fromLumi) lumiFlush = f(f(Math.pow(f(toLumi - fromLumi), 0.7)) * 560);
  else {
    lumiFlush = f((fromLumi - toLumi) * 80);
    hsDist = Math.min(f(0.67 * tv + 0.33 * fv), hsDist);
  }
  const hsFlush = f(230 * hsDist);
  const third = f(Math.sqrt(hsFlush * hsFlush + lumiFlush * lumiFlush - 2 * hsFlush * lumiFlush * f(Math.cos(toRad(120)))));
  return Math.trunc(Math.max(third, 60));
}


export function calcFlushVol(from: string, to: string, minFlush: number, dataset: number, maxFlush = MAX_FLUSH_VOLUME): number {
  const a = parseColour(from), b = parseColour(to);
  const src: RGB = a.alpha === 0 ? [255, 255, 255] : a.rgb;
  const dst: RGB = b.alpha === 0 ? [255, 255, 255] : b.rgb;
  if (dataset !== 0) {
    const p = predictor(dataset).predict(src, dst);
    if (p !== null) return Math.min(Math.trunc(p), maxFlush);
  }
  let vol = flushRgb(src, dst, dataset);
  const isFromDark = luminance(...src) > 180 / 255;
  const isToLight = luminance(...dst) < 75 / 255;
  if (dataset !== 0 && isFromDark && isToLight) vol = f(vol * 1.3);
  vol = f(vol + minFlush);
  return Math.min(Math.trunc(vol), maxFlush);
}



const at = (v: ConfigValue | undefined, i: number): string | undefined => {
  if (v === undefined || v === null) return undefined;
  const arr = Array.isArray(v) ? v : String(v).split(',');
  return arr.length ? String(arr[Math.min(i, arr.length - 1)]) : undefined;
};
const num = (v: string | undefined, d = 0) => (v === undefined || v === '' || !Number.isFinite(Number(v)) ? d : Number(v));


export function minFlushVolumes(printer: Config, filaments: Config[], nozzle = 0): number[] {
  const nozzleVolume = Math.trunc(num(at(printer.nozzle_volume, nozzle)));
  const level = num(at(printer.enable_long_retraction_when_cut, 0));
  const activated = num(at(printer.long_retractions_when_cut, nozzle)) === 1;
  const printerCut = num(at(printer.retraction_distances_when_cut, nozzle), 18);
  return filaments.map((fc) => {
    let retract = level && activated ? printerCut : 0;
    const filActivated = num(at(fc.filament_long_retractions_when_cut, 0));
    const filCut = at(fc.filament_retraction_distances_when_cut, 0);
    if (filActivated === 0) retract = 0;
    else if (filActivated === 1 && level === 2) retract = filCut !== undefined && filCut !== 'nil' ? Math.trunc(num(filCut, 18)) : printerCut;
    return Math.trunc(nozzleVolume - ((Math.PI * 1.75 * 1.75) / 4) * retract);
  });
}


export function isSupportFilament(filaments: Config[], i: number): boolean {
  const type = (c: Config) => at(c.filament_type, 0) ?? '';
  const t = type(filaments[i]);
  const has = (list: string[]) => filaments.some((c) => list.includes(type(c)));
  if (t === 'PETG' && has(['PLA'])) return true;
  if (t === 'PLA' && has(['PETG', 'TPU', 'TPU-AMS'])) return true;
  const v = at(filaments[i].filament_is_support, 0);
  return v === '1' || v === 'true';
}


export function calcFlushingVolumes(colours: string[], printer: Config, filaments: Config[], nozzle = 0): number[] {
  const n = colours.length;
  const dataset = num(at(printer.nozzle_flush_dataset, nozzle));
  const mins = minFlushVolumes(printer, filaments, nozzle);
  const out: number[] = [];
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      if (i === j) {
        out.push(0);
        continue;
      }
      let v: number;
      if (isSupportFilament(filaments, j)) v = FLUSH_VOLUME_TO_SUPPORT;
      else {
        v = calcFlushVol(colours[i], colours[j], mins[i] ?? 0, dataset);
        if (isSupportFilament(filaments, i)) v = Math.max(MIN_FLUSH_VOLUME_FROM_SUPPORT, v);
      }
      out.push(v);
    }
  return out;
}


export function predictorMinFlush(dataset: number): number {
  const p = predictor(dataset);
  return p.valid && p.table.size ? Math.trunc(Math.min(...p.table.values())) : Number.MAX_SAFE_INTEGER;
}
