import type { Config } from '../api/types';

export type ScaleBasis = 'nozzle' | 'layer' | 'perLayer';
export interface ScaleRow { key: string; basis: ScaleBasis; factor: number }
export interface ScaleContext { nozzle: number; layerHeight: number }
export interface AutoRecord { schema: 3; scales: ScaleRow[]; preferredHeight?: number }

export const DEFAULT_SCALES: readonly ScaleRow[] = [
  { key: 'line_width', basis: 'nozzle', factor: 1.125 },
  { key: 'outer_wall_line_width', basis: 'nozzle', factor: 1 },
  { key: 'inner_wall_line_width', basis: 'nozzle', factor: 1.125 },
  { key: 'sparse_infill_line_width', basis: 'nozzle', factor: 1.125 },
  { key: 'internal_solid_infill_line_width', basis: 'nozzle', factor: 1.125 },
  { key: 'top_surface_line_width', basis: 'nozzle', factor: 1 },
  { key: 'support_line_width', basis: 'nozzle', factor: 1 },
  { key: 'initial_layer_line_width', basis: 'nozzle', factor: 1.25 },
  { key: 'initial_layer_print_height', basis: 'nozzle', factor: 0.5 },
  { key: 'support_top_z_distance', basis: 'layer', factor: 1 },
  { key: 'support_bottom_z_distance', basis: 'layer', factor: 1 },
  { key: 'top_shell_layers', basis: 'perLayer', factor: 1 },
  { key: 'bottom_shell_layers', basis: 'perLayer', factor: 0.6 },
];

const round = (x: number) => Math.round(x * 1000) / 1000;
const isWidth = (key: string) => key === 'line_width' || key.endsWith('_line_width');
export const clampHeight = (layerHeight: number, nozzle: number) => nozzle > 0 ? Math.min(layerHeight, round(nozzle * 0.75)) : layerHeight;

export function scaledValue(row: ScaleRow, ctx: ScaleContext): number {
  const h = clampHeight(ctx.layerHeight, ctx.nozzle);
  const value = row.basis === 'nozzle' ? row.factor * ctx.nozzle : row.basis === 'layer' ? row.factor * h : Math.ceil(row.factor / h - 1e-6);
  return row.basis === 'perLayer' ? value : round(isWidth(row.key) ? Math.min(1.5 * ctx.nozzle, Math.max(0.9 * ctx.nozzle, value)) : value);
}
export function scaleValues(scales: readonly ScaleRow[], ctx: ScaleContext): Config {
  if (!(ctx.nozzle > 0) || !(ctx.layerHeight > 0)) return {};
  return Object.fromEntries(scales.map((row) => [row.key, String(scaledValue(row, ctx))]));
}
export const scalesOf = (record: unknown): ScaleRow[] => {
  const scales = (record as { auto?: { scales?: unknown } } | undefined)?.auto?.scales;
  return Array.isArray(scales) ? scales.filter((r): r is ScaleRow => !!r && typeof r.key === 'string' && Number.isFinite(r.factor) && ['nozzle', 'layer', 'perLayer'].includes(r.basis)) : [];
};
export function heightsFor(nozzle: number, min?: number, max?: number): number[] {
  const lo = min && min > 0 ? min : Math.max(0.04, Math.round(nozzle * 20) / 100), hi = Math.min(max && max > 0 ? max : nozzle * 0.7, nozzle * 0.75);
  const out: number[] = [];
  for (let h = lo; h <= hi + 1e-6; h += 0.04) out.push(Math.round(h * 100) / 100);
  return out.length ? out : [lo];
}
