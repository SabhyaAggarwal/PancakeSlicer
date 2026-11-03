import { MoveType } from './format';
import type { PreviewData } from './data';

export type RGB = [number, number, number];

export const ROLE_COLORS: RGB[] = [
  [230, 179, 179],
  [255, 230, 77],
  [255, 125, 56],
  [31, 31, 255],
  [176, 48, 41],
  [150, 84, 204],
  [240, 64, 64],
  [102, 92, 199],
  [255, 140, 105],
  [77, 128, 186],
  [77, 128, 186],
  [255, 255, 255],
  [0, 135, 110],
  [0, 59, 110],
  [0, 255, 0],
  [0, 128, 0],
  [0, 64, 0],
  [179, 227, 171],
  [94, 209, 148],
  [128, 128, 128],
];

export const OPTION_COLORS = {
  travel: [56, 72, 155] as RGB,
  wipe: [255, 255, 0] as RGB,
  retract: [205, 34, 214] as RGB,
  unretract: [73, 173, 207] as RGB,
  seam: [230, 230, 230] as RGB,
  toolChange: [193, 190, 99] as RGB,
  colorChange: [218, 148, 139] as RGB,
  pausePrint: [82, 240, 131] as RGB,
  customGCode: [226, 210, 67] as RGB,
};

export function optionColor(type: number): RGB {
  switch (type) {
    case MoveType.Travel: return OPTION_COLORS.travel;
    case MoveType.Wipe: return OPTION_COLORS.wipe;
    case MoveType.Retract: return OPTION_COLORS.retract;
    case MoveType.Unretract: return OPTION_COLORS.unretract;
    case MoveType.Seam: return OPTION_COLORS.seam;
    case MoveType.ToolChange: return OPTION_COLORS.toolChange;
    case MoveType.ColorChange: return OPTION_COLORS.colorChange;
    case MoveType.PausePrint: return OPTION_COLORS.pausePrint;
    case MoveType.CustomGCode: return OPTION_COLORS.customGCode;
    default: return DUMMY_COLOR;
  }
}

export const DUMMY_COLOR: RGB = [64, 64, 64];

export const RANGE_PALETTE: RGB[] = [
  [11, 44, 122], [19, 89, 133], [28, 136, 145], [4, 214, 15], [170, 242, 0], [252, 249, 3],
  [245, 206, 10], [227, 136, 32], [209, 104, 48], [194, 82, 60], [148, 38, 22],
];

export const NONPLANAR_PLANAR_COLOR: RGB = [140, 140, 140];
const NONPLANAR_PALETTE: RGB[] = [[230, 159, 0], [86, 180, 233], [0, 158, 115], [240, 228, 66], [0, 114, 178], [213, 94, 0], [204, 121, 167]];
export const NONPLANAR_HUES = NONPLANAR_PALETTE.length;
export function nonplanarIndexColor(index: number): RGB {
  return index < 0 ? [230, 230, 230] : NONPLANAR_PALETTE[index % NONPLANAR_PALETTE.length];
}

export class ColorRange {
  min = Infinity;
  max = -Infinity;
  count = 0;
  constructor(public log = false) {}
  update(v: number) {
    v = Math.fround(v);
    if (v !== this.min && v !== this.max) ++this.count;
    if (v < this.min) this.min = v;
    if (v > this.max) this.max = v;
  }
  private step() {
    const n = RANGE_PALETTE.length - 1;
    if (this.log) return this.min !== 0 ? Math.log(this.max / this.min) / n : 0;
    return (this.max - this.min) / n;
  }
  colorAt(value: number): RGB {
    const f = Math.fround;
    const v = f(Math.min(Math.max(value, this.min), this.max));
    const step = f(this.step());
    let t = 0;
    if (step > 0) t = this.log ? (this.min !== 0 ? f(f(Math.log(f(v / this.min))) / step) : 0) : f(f(v - this.min) / step);
    const maxIdx = RANGE_PALETTE.length - 1;
    const lo = Math.min(Math.max(Math.trunc(t), 0), maxIdx);
    const hi = Math.min(lo + 1, maxIdx);
    const u = f(Math.min(Math.max(f(t - lo), 0), 1));
    const a = RANGE_PALETTE[lo], b = RANGE_PALETTE[hi];
    const w = f(1 - u);
    return [0, 1, 2].map((k) => Math.trunc(f(f(w * a[k]) + f(u * b[k])))) as RGB;
  }
  values(): number[] {
    if (!(this.max >= this.min)) return [];
    if (this.count <= 1) return [this.min];
    if (this.count === 2) return [this.min, this.max];
    const s = this.step();
    return RANGE_PALETTE.map((_, i) => (this.log ? Math.exp(Math.log(this.min) + i * s) : this.min + i * s));
  }
}

export function roundToBin(value: number): number {
  const scale = [100, 1000, 10000, 100000, 1000000];
  const threshold = [0.095, 0.0095, 0.00095, 0.000095, 0.0000095];
  let i = 0;
  while (value < threshold[i] && i < 4) ++i;
  return Math.floor(value * scale[i] + 0.5) / scale[i];
}

export type ViewType =
  | 'feature' | 'filament' | 'speed' | 'height' | 'width' | 'flow' | 'layer_time' | 'layer_time_log'
  | 'fan' | 'temperature' | 'acceleration' | 'jerk' | 'pressure_advance' | 'nonplanar'
  | 'actual_speed' | 'actual_flow' | 'clearance';

export const EXTENDED_VIEWS = new Set<ViewType>(['actual_speed', 'actual_flow', 'clearance']);

export const VIEW_TYPES: { key: ViewType; label: string }[] = [
  { key: 'feature', label: 'Line Type' },
  { key: 'filament', label: 'Filament' },
  { key: 'speed', label: 'Speed' },
  { key: 'actual_speed', label: 'Actual Speed' },
  { key: 'acceleration', label: 'Acceleration' },
  { key: 'jerk', label: 'Jerk' },
  { key: 'clearance', label: 'Clearance' },
  { key: 'nonplanar', label: 'Non-planar' },
  { key: 'height', label: 'Layer Height' },
  { key: 'width', label: 'Line Width' },
  { key: 'flow', label: 'Flow' },
  { key: 'actual_flow', label: 'Actual Flow' },
  { key: 'layer_time', label: 'Layer Time' },
  { key: 'layer_time_log', label: 'Layer Time (log)' },
  { key: 'fan', label: 'Fan Speed' },
  { key: 'temperature', label: 'Temperature' },
  { key: 'pressure_advance', label: 'Pressure Advance' },
];

export type NonplanarColor = 'shell' | 'band' | 'patch' | 'bead_height' | 'slope' | 'curved';
export const CURVED_COLOR: RGB = [22, 179, 160];
export const NONPLANAR_COLORS: { key: NonplanarColor; label: string }[] = [
  { key: 'shell', label: 'Shell' },
  { key: 'band', label: 'Band' },
  { key: 'patch', label: 'Patch' },
  { key: 'bead_height', label: 'Bead height' },
  { key: 'slope', label: 'Slope' },
];

export interface Visibility {
  travel: boolean;
  wipe: boolean;
  retract: boolean;
  unretract: boolean;
  seam: boolean;
  hiddenRoles: Set<number>;
}

const MAPPED_ROLES = [6, 8];

export function drawMasks(v: Visibility, surface = false): { roleMask: number; typeMask: number; optionMask: number } {
  let roleMask = 0;
  for (let r = 0; r < 20; ++r) if (!v.hiddenRoles.has(r) && !(surface && MAPPED_ROLES.includes(r))) roleMask |= 1 << r;
  let typeMask = 1 << MoveType.Extrude;
  if (v.travel) typeMask |= 1 << MoveType.Travel;
  if (v.wipe) typeMask |= 1 << MoveType.Wipe;
  let optionMask = 0;
  if (v.retract) optionMask |= 1 << MoveType.Retract;
  if (v.unretract) optionMask |= 1 << MoveType.Unretract;
  if (v.seam) optionMask |= 1 << MoveType.Seam;
  return { roleMask, typeMask, optionMask };
}

export interface Ranges {
  height: ColorRange;
  width: ColorRange;
  speed: ColorRange;
  actualSpeed: ColorRange;
  actualFlow: ColorRange;
  fan: ColorRange;
  temperature: ColorRange;
  flow: ColorRange;
  acceleration: ColorRange;
  jerk: ColorRange;
  pa: ColorRange;
  layerTime: ColorRange;
  layerTimeLog: ColorRange;
  shell: ColorRange;
  beadHeight: ColorRange;
  slope: ColorRange;
}

interface CachedRanges { ranges: Ranges; loaded: number; slope: Float32Array | null; vis: Visibility }
const rangeCache = new WeakMap<PreviewData, Map<number, CachedRanges>>();

export function updateCachedRanges(d: PreviewData) {
  for (const entry of rangeCache.get(d)?.values() ?? []) computeRanges(d, entry.vis);
}

export function computeRanges(d: PreviewData, vis: Visibility): Ranges {
  const key = Number(vis.travel) | Number(vis.wipe) << 1 | Number(vis.hiddenRoles.has(18)) << 2;
  let cache = rangeCache.get(d);
  if (!cache) rangeCache.set(d, cache = new Map());
  let entry = cache.get(key);
  if (entry && (entry.loaded > d.loaded || entry.slope !== d.slope)) entry = undefined;
  const r: Ranges = entry?.ranges ?? {
    height: new ColorRange(), width: new ColorRange(), speed: new ColorRange(), actualSpeed: new ColorRange(), actualFlow: new ColorRange(), fan: new ColorRange(),
    temperature: new ColorRange(), flow: new ColorRange(), acceleration: new ColorRange(), jerk: new ColorRange(), pa: new ColorRange(),
    layerTime: new ColorRange(), layerTimeLog: new ColorRange(true), shell: new ColorRange(),
    beadHeight: new ColorRange(), slope: new ColorRange(),
  };
  const s = d.states!;
  const custom = !vis.hiddenRoles.has(18);
  const ext = d.actual.length > 0;
  for (let i = Math.max(1, entry?.loaded ?? 1); i < d.loaded; ++i) {
    const t = d.type[i] & 0x7f;
    const st = d.state[i];
    if (t === MoveType.Extrude) {
      r.height.update(roundToBin(d.height[i]));
      if (d.role[i] !== 18 || custom) {
        r.width.update(roundToBin(d.width[i]));
        r.flow.update(roundToBin(d.mm3[i] * d.feed[i]));
        if (ext) r.actualFlow.update(roundToBin(d.mm3[i] * d.actual[i]));
      }
      r.fan.update(roundToBin(s.fan[st]));
      r.temperature.update(roundToBin(s.temp[st]));
      if (s.shell[st] !== 0xff) {
        r.shell.update(s.shell[st]);
        r.beadHeight.update(roundToBin(d.height[i]));
        if (d.slope) r.slope.update(Math.round(d.slope[i]));
      }
      if (s.pa[st] >= 0) r.pa.update(s.pa[st]);
    }
    if ((t === MoveType.Travel && vis.travel) || (t === MoveType.Wipe && vis.wipe) || t === MoveType.Extrude) {
      r.speed.update(d.feed[i]);
      if (ext) r.actualSpeed.update(d.actual[i]);
      r.acceleration.update(s.accel[st]);
      r.jerk.update(s.jerk[st]);
    }
  }
  r.layerTime = new ColorRange();
  r.layerTimeLog = new ColorRange(true);
  for (const lt of d.layerTimes) {
    r.layerTime.update(lt);
    r.layerTimeLog.update(lt);
  }
  cache.set(key, { ranges: r, loaded: d.loaded, slope: d.slope, vis: { ...vis, hiddenRoles: new Set(vis.hiddenRoles) } });
  return r;
}

export interface ColorContext {
  view: ViewType;
  nonplanar: NonplanarColor;
  ranges: Ranges;
  extruderColors: RGB[];
  colorPrintColors: RGB[];
  clearance?: { margin: number; floor: number };
}

export function clearanceColor(clearance: number, margin: number, floor: number): RGB {
  if (Number.isNaN(clearance)) return [128, 128, 128];
  const red: RGB = [220, 55, 45], amber: RGB = [245, 180, 45], green: RGB = [35, 175, 100];
  if (clearance <= 0) return red;
  floor = Math.max(floor, 0.001);
  if (clearance >= floor) return green;
  margin = Math.min(Math.max(margin, 0.001), floor);
  const low = clearance < margin;
  const a = low ? red : amber, b = low ? amber : green;
  const t = low ? clearance / margin : (clearance - margin) / (floor - margin);
  return [0, 1, 2].map((k) => Math.trunc(a[k] + Math.fround(t) * (b[k] - a[k]))) as RGB;
}

export function hexToRgb(h: string | undefined): RGB {
  const m = /^#?([0-9a-f]{6})/i.exec(h ?? '');
  if (!m) return [242, 117, 78];
  const n = parseInt(m[1], 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

export function colorVertices(d: PreviewData, c: ColorContext, out: Uint8Array, from: number, to: number) {
  const s = d.states!;
  const { ranges: r, view } = c;
  const put = (i: number, col: RGB) => {
    out[i * 4] = col[0];
    out[i * 4 + 1] = col[1];
    out[i * 4 + 2] = col[2];
    out[i * 4 + 3] = 255;
  };
  const npColor = (i: number, st: number): RGB => {
    switch (c.nonplanar) {
      case 'band': return nonplanarIndexColor(s.band[st]);
      case 'patch': return nonplanarIndexColor(s.patch[st]);
      case 'bead_height': return r.beadHeight.colorAt(d.height[i]);
      case 'slope': return r.slope.colorAt(d.slope ? d.slope[i] : 0);
      case 'curved': return CURVED_COLOR;
      default: return r.shell.colorAt(s.shell[st]);
    }
  };
  for (let i = from; i < to; ++i) {
    const t = d.type[i] & 0x7f;
    const st = d.state[i];
    if (t === MoveType.Noop) { put(i, DUMMY_COLOR); continue; }
    const extr = t === MoveType.Extrude;
    const travel = t === MoveType.Travel;
    if (view === 'clearance' && (extr || travel || t === MoveType.Wipe)) {
      const sc = c.clearance ?? { margin: 0.3, floor: 1 };
      put(i, clearanceColor(d.clearance.length ? d.clearance[i] : NaN, sc.margin, sc.floor));
      continue;
    }
    if (view === 'nonplanar' && extr) { put(i, s.shell[st] === 0xff ? NONPLANAR_PLANAR_COLOR : npColor(i, st)); continue; }
    if ((t === MoveType.Wipe && view !== 'speed' && view !== 'actual_speed' && view !== 'acceleration' && view !== 'jerk') || (t >= MoveType.Retract && t <= MoveType.CustomGCode)) {
      put(i, optionColor(t));
      continue;
    }
    let col: RGB;
    switch (view) {
      case 'feature': col = travel ? optionColor(t) : ROLE_COLORS[d.role[i]] ?? DUMMY_COLOR; break;
      case 'height': col = travel ? optionColor(t) : r.height.colorAt(d.height[i]); break;
      case 'width': col = travel ? optionColor(t) : r.width.colorAt(d.width[i]); break;
      case 'speed': col = r.speed.colorAt(d.feed[i]); break;
      case 'actual_speed': col = r.actualSpeed.colorAt(d.actual.length ? d.actual[i] : 0); break;
      case 'actual_flow': col = travel ? optionColor(t) : r.actualFlow.colorAt(d.actual.length ? d.mm3[i] * d.actual[i] : 0); break;
      case 'acceleration': col = r.acceleration.colorAt(s.accel[st]); break;
      case 'jerk': col = r.jerk.colorAt(s.jerk[st]); break;
      case 'fan': col = travel ? optionColor(t) : r.fan.colorAt(s.fan[st]); break;
      case 'temperature': col = travel ? optionColor(t) : r.temperature.colorAt(s.temp[st]); break;
      case 'pressure_advance': col = travel ? optionColor(t) : r.pa.colorAt(s.pa[st]); break;
      case 'flow': col = travel ? optionColor(t) : r.flow.colorAt(d.mm3[i] * d.feed[i]); break;
      case 'layer_time': col = travel ? optionColor(t) : r.layerTime.colorAt(d.layerTimes[d.layer[i]] ?? 0); break;
      case 'layer_time_log': col = travel ? optionColor(t) : r.layerTimeLog.colorAt(d.layerTimes[d.layer[i]] ?? 0); break;
      case 'nonplanar': col = optionColor(t); break;
      case 'filament': {
        const pal = c.colorPrintColors.length ? c.colorPrintColors : c.extruderColors;
        col = d.colorPrintOptionLayers.has(d.layer[i]) ? DUMMY_COLOR : pal[d.cp[i] % Math.max(1, pal.length)] ?? DUMMY_COLOR;
        break;
      }
      default: col = DUMMY_COLOR;
    }
    put(i, col);
  }
}
