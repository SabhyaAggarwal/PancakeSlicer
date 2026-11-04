import type { Config } from '../api/types';
import rules from '../../../server/profile_rules.json';

export type ScaleBasis = 'nozzle' | 'layer' | 'perLayer';
export interface ScaleRow { key: string; basis: ScaleBasis; factor: number }
export interface ScaleContext { nozzle: number; layerHeight: number }
export interface AutoRecord { schema: 3; scales: ScaleRow[]; preferredHeight?: number }

export const DEFAULT_SCALES: ScaleRow[] = rules.defaultScales.map((r) => ({ ...r, basis: r.basis as ScaleBasis }));

const round = (x: number) => Math.round(x * rules.round) / rules.round;
const isWidth = (key: string) => key === 'line_width' || key.endsWith('_line_width');
export const clampHeight = (layerHeight: number, nozzle: number) => nozzle > 0 ? Math.min(layerHeight, round(nozzle * rules.maxLayerOfNozzle)) : layerHeight;
const [minWidth, maxWidth] = rules.lineWidthOfNozzle;

export function scaledValue(row: ScaleRow, ctx: ScaleContext): number {
  const h = clampHeight(ctx.layerHeight, ctx.nozzle);
  const value = row.basis === 'nozzle' ? row.factor * ctx.nozzle : row.basis === 'layer' ? row.factor * h : Math.ceil(row.factor / h - 1e-6);
  return row.basis === 'perLayer' ? value : round(isWidth(row.key) ? Math.min(maxWidth * ctx.nozzle, Math.max(minWidth * ctx.nozzle, value)) : value);
}
export function scaleValues(scales: readonly ScaleRow[], ctx: ScaleContext): Config {
  if (!(ctx.nozzle > 0) || !(ctx.layerHeight > 0)) return {};
  return Object.fromEntries(scales.map((row) => [row.key, String(scaledValue(row, ctx))]));
}
export function effectiveShells(config: Config, side: 'top' | 'bottom', layerHeight: number): { layers: number; effective: number; thickness: number; lifted: boolean } {
  const num = (v: Config[string] | undefined) => { const x = Number(Array.isArray(v) ? v[0] : v); return Number.isFinite(x) ? x : 0; };
  const layers = Math.max(0, Math.round(num(config[`${side}_shell_layers`]))), thickness = num(config[`${side}_shell_thickness`]);
  const minimum = thickness > 0 && layerHeight > 0 && layers > 0 ? Math.ceil(thickness / layerHeight - 1e-6) : 0;
  return { layers, effective: Math.max(layers, minimum), thickness, lifted: minimum > layers };
}
export const scalesOf = (record: unknown): ScaleRow[] => {
  const scales = (record as { auto?: { scales?: unknown } } | undefined)?.auto?.scales;
  return Array.isArray(scales) ? scales.filter((r): r is ScaleRow => !!r && typeof r.key === 'string' && Number.isFinite(r.factor) && ['nozzle', 'layer', 'perLayer'].includes(r.basis)) : [];
};
export function heightsFor(nozzle: number, min?: number, max?: number): number[] {
  const { step, minOfNozzle, minFloor, maxOfNozzle } = rules.heights;
  const lo = min && min > 0 ? min : Math.max(minFloor, Math.round(nozzle * minOfNozzle * 100) / 100), hi = Math.min(max && max > 0 ? max : nozzle * maxOfNozzle, nozzle * rules.maxLayerOfNozzle);
  const out: number[] = [];
  for (let h = lo; h <= hi + 1e-6; h += step) out.push(Math.round(h * 100) / 100);
  return out.length ? out : [lo];
}
