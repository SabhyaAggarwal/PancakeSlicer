import type { Config } from '../api/types';

export type Speed = 'quality' | 'balanced' | 'fast';
export const SPEEDS: readonly Speed[] = ['quality', 'balanced', 'fast'];
export interface AutoInputs {
  nozzle: number; layerHeight: number; speed: Speed;
  maxSpeed?: number; maxAccel?: number;
  maxVolumetric?: number;
  intent: Config;
}
const num = (v: unknown): number => { const x = Number(Array.isArray(v) ? v[0] : v); return Number.isFinite(x) ? x : 0; };
const mm = (x: number) => String(Math.round(x * 1000) / 1000);
const STYLE_SPEED: Record<Speed, number> = { quality: 0.6, balanced: 1, fast: 1.4 };
const STYLE_ACCEL: Record<Speed, number> = { quality: 0.5, balanced: 0.8, fast: 1 };
const ROLE_SPEED: [key: string, fraction: number, width: string][] = [
  ['outer_wall_speed', 0.4, 'outer_wall_line_width'], ['inner_wall_speed', 0.6, 'inner_wall_line_width'],
  ['sparse_infill_speed', 0.54, 'sparse_infill_line_width'], ['internal_solid_infill_speed', 0.5, 'internal_solid_infill_line_width'],
  ['top_surface_speed', 0.4, 'top_surface_line_width'], ['gap_infill_speed', 0.5, 'inner_wall_line_width'],
  ['overhang_1_4_speed', 0.1, 'outer_wall_line_width'], ['overhang_2_4_speed', 0.06, 'outer_wall_line_width'],
  ['overhang_3_4_speed', 0.04, 'outer_wall_line_width'], ['overhang_4_4_speed', 0.03, 'outer_wall_line_width'],
];
const WIDTH: [key: string, factor: number][] = [
  ['line_width', 1.125], ['outer_wall_line_width', 1], ['inner_wall_line_width', 1.125], ['sparse_infill_line_width', 1.125],
  ['internal_solid_infill_line_width', 1.125], ['top_surface_line_width', 1.125], ['support_line_width', 1.125], ['initial_layer_line_width', 1.25],
];
export const AUTO_KEYS: readonly string[] = [
  'layer_height', 'initial_layer_print_height', 'top_shell_layers', 'bottom_shell_layers', 'support_top_z_distance', 'support_bottom_z_distance',
  'support_threshold_angle', 'ironing_flow', 'default_acceleration', 'outer_wall_acceleration', 'top_surface_acceleration',
  ...WIDTH.map(([k]) => k), ...ROLE_SPEED.map(([k]) => k),
];

export function autoValues(i: AutoInputs): Config {
  const { nozzle, layerHeight: h, speed } = i, out: Config = { layer_height: mm(h) };
  if (!(nozzle > 0) || !(h > 0)) return {};
  const width = Object.fromEntries(WIDTH.map(([k, f]) => [k, nozzle * f]));
  for (const [k, w] of Object.entries(width)) out[k] = mm(w);
  out.initial_layer_print_height = mm(Math.min(0.2, nozzle * 0.5));
  for (const [thickness, layers] of [['top_shell_thickness', 'top_shell_layers'], ['bottom_shell_thickness', 'bottom_shell_layers']] as const) {
    const t = num(i.intent[thickness]);
    if (t > 0) out[layers] = String(Math.max(2, Math.ceil(t / h - 1e-6)));
  }
  out.support_top_z_distance = mm(h); out.support_bottom_z_distance = mm(h);
  out.support_threshold_angle = String(Math.round(15 + 25 * Math.min(1, Math.max(0, (h - 0.08) / 0.2))));
  out.ironing_flow = h < 0.1 ? '8%' : '10%';
  const top = i.maxSpeed || 500, volumetric = i.maxVolumetric && i.maxVolumetric > 0 ? i.maxVolumetric : Infinity;
  for (const [key, fraction, widthKey] of ROLE_SPEED) {
    const cap = top * fraction * STYLE_SPEED[speed], limit = volumetric / ((out[widthKey] ? num(out[widthKey]) : nozzle * 1.125) * h);
    out[key] = String(Math.round(Math.min(cap, limit)));
  }
  const accel = (i.maxAccel || 10000) * STYLE_ACCEL[speed];
  out.default_acceleration = String(Math.round(accel));
  out.outer_wall_acceleration = String(Math.round(accel * 0.5));
  out.top_surface_acceleration = String(Math.round(accel * 0.4));
  return out;
}
export function heightsFor(nozzle: number, min?: number, max?: number): number[] {
  const lo = min && min > 0 ? min : Math.max(0.04, Math.round(nozzle * 20) / 100), hi = max && max > 0 ? max : nozzle * 0.7;
  const out: number[] = [];
  for (let h = lo; h <= hi + 1e-6; h += 0.04) out.push(Math.round(h * 100) / 100);
  return out.length ? out : [lo];
}
