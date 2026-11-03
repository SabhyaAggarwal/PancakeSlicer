import type { Config, ConfigValue, PresetType } from '../api/types';
import { matchesPrinter, type WorksWithRule } from './works-with.ts';

export interface DynamicContext { printerModel: string; nozzle: number; layerHeight: number }
export type PrintContext = Omit<DynamicContext, 'layerHeight'> & { printerId?: string };
export interface DynamicWhen { printerModel?: string; nozzle?: number; layerHeight?: number }
export interface DynamicLayer { id: string; when: DynamicWhen; values: Config }
export interface DynamicProfile {
  schema: 1;
  reference: { nozzle: number; layerHeight: number };
  layers: DynamicLayer[];
}
export interface DynamicRecord { type: PresetType; dynamic?: DynamicProfile; applicability?: WorksWithRule }
export type DynamicScope = 'all' | 'nozzle' | 'layer' | 'combo';

export const LINE_WIDTH_KEYS = ['line_width', 'initial_layer_line_width', 'outer_wall_line_width', 'inner_wall_line_width',
  'top_surface_line_width', 'sparse_infill_line_width', 'internal_solid_infill_line_width', 'support_line_width',
  'bridge_line_width', 'skin_infill_line_width', 'skeleton_infill_line_width'];
const AXES = ['nozzle', 'layerHeight', 'printerModel'] as const;
type Axis = typeof AXES[number];

export const sameMm = (a: number, b: number) => Math.abs(a - b) < 0.0005;
const round3 = (n: number) => Math.round(n * 1000) / 1000;
const axesOf = (when: DynamicWhen) => AXES.filter((a) => when[a] !== undefined);
const sameWhen = (a: DynamicWhen, b: DynamicWhen) => AXES.every((x) => a[x] === undefined ? b[x] === undefined
  : b[x] !== undefined && (x === 'printerModel' ? a[x] === b[x] : sameMm(a[x] as number, b[x] as number)));
const covers = (a: DynamicWhen, b: DynamicWhen) => axesOf(b).every((x) => a[x] !== undefined
  && (x === 'printerModel' ? a[x] === b[x] : sameMm(a[x] as number, b[x] as number)));
const applies = (when: DynamicWhen, ctx: Partial<DynamicContext>) => covers(ctx as DynamicWhen, when);

export function rank(when: DynamicWhen): number {
  const named = axesOf(when).map((a) => 3 - AXES.indexOf(a)).sort((a, b) => b - a);
  return named.reduce((r, p) => r * 4 + p, 0) + named.length * 1000;
}

const canon = (v: ConfigValue | undefined): string => {
  if (v === undefined) return '';
  const one = (x: unknown) => {
    const n = typeof x === 'number' ? x : typeof x === 'string' && x.trim() !== '' && !x.trim().endsWith('%') ? Number(x) : NaN;
    return Number.isFinite(n) ? `#${n}` : String(x);
  };
  return Array.isArray(v) ? `[${v.map(one).join(',')}]` : one(v);
};
export const sameDynamicValue = (a: ConfigValue | undefined, b: ConfigValue | undefined) => canon(a) === canon(b);

const ordered = (d: DynamicProfile) => [...d.layers].sort((a, b) => rank(a.when) - rank(b.when));
const usable = (p: DynamicRecord, ctx: Partial<DynamicContext>) =>
  p.type === 'process' ? ctx : { printerModel: ctx.printerModel, nozzle: ctx.nozzle };

export function layerSources(p: DynamicRecord, ctx: Partial<DynamicContext>): Record<string, DynamicLayer> {
  const out: Record<string, DynamicLayer> = {};
  if (!p.dynamic) return out;
  const c = usable(p, ctx);
  for (const layer of ordered(p.dynamic)) if (applies(layer.when, c)) for (const key of Object.keys(layer.values)) out[key] = layer;
  return out;
}

export function resolveDynamic(p: DynamicRecord, ctx: DynamicContext, below: Config = {}): Config {
  const d = p.dynamic;
  if (!d) return {};
  const sources = layerSources(p, ctx), out: Config = {};
  for (const [key, layer] of Object.entries(sources)) out[key] = layer.values[key];
  if (p.type !== 'process') return out;
  out.layer_height = String(round3(ctx.layerHeight));
  if (!sameMm(ctx.nozzle, d.reference.nozzle)) for (const key of LINE_WIDTH_KEYS) {
    if (sources[key]?.when.nozzle !== undefined) continue;
    const value = out[key] ?? below[key];
    const n = typeof value === 'number' ? value : typeof value === 'string' && !value.trim().endsWith('%') ? Number(value) : NaN;
    if (!(n > 0)) continue;
    const scaled = round3(n * ctx.nozzle / d.reference.nozzle);
    out[key] = typeof value === 'number' ? scaled : String(scaled);
  }
  return out;
}

export function availableHeights(p: DynamicRecord, ctx: PrintContext): number[] {
  const d = p.dynamic;
  if (!d || p.type !== 'process' || !(ctx.nozzle > 0) || p.applicability && !matchesPrinter(p.applicability, { id: ctx.printerId, ...ctx })) return [];
  const heights: number[] = [];
  const add = (h: number) => { if (h >= 0.25 * ctx.nozzle - 0.0005 && h <= 0.75 * ctx.nozzle + 0.0005 && !heights.some((x) => sameMm(x, h))) heights.push(round3(h)); };
  add(d.reference.layerHeight);
  for (const layer of d.layers) if (layer.when.layerHeight !== undefined && applies({ ...layer.when, layerHeight: undefined }, ctx)) add(layer.when.layerHeight);
  return heights.sort((a, b) => a - b);
}
export const nearestHeight = (heights: number[], h: number): number =>
  heights.reduce((best, x) => Math.abs(x - h) < Math.abs(best - h) - 0.0005 ? x : best, heights[0]);

export const coversPrinter = (p: DynamicRecord, ctx: PrintContext) => !!p.dynamic
  && (p.type === 'process' ? availableHeights(p, ctx).length > 0 : !p.applicability || matchesPrinter(p.applicability, { id: ctx.printerId, ...ctx }));

export interface DynamicConflict { key: string; winner: string; loser: string }
export function dynamicConflicts(p: DynamicRecord, ctx: DynamicContext): DynamicConflict[] {
  if (!p.dynamic) return [];
  const c = usable(p, ctx), live = ordered(p.dynamic).filter((l) => applies(l.when, c)), out: DynamicConflict[] = [];
  for (let i = 0; i < live.length; i++) for (let j = i + 1; j < live.length; j++) {
    const [lo, hi] = [live[i], live[j]];
    if (covers(lo.when, hi.when) || covers(hi.when, lo.when)) continue;
    const both = { ...lo.when, ...hi.when };
    for (const key of Object.keys(lo.values)) {
      if (!(key in hi.values) || sameDynamicValue(lo.values[key], hi.values[key])) continue;
      if (live.some((l) => key in l.values && covers(l.when, both))) continue;
      out.push({ key, winner: hi.id, loser: lo.id });
    }
  }
  return out.sort((a, b) => a.key.localeCompare(b.key) || a.winner.localeCompare(b.winner) || a.loser.localeCompare(b.loser));
}

export function scopeWhen(scope: DynamicScope, ctx: DynamicContext): DynamicWhen | undefined {
  return scope === 'all' ? undefined : scope === 'nozzle' ? { nozzle: ctx.nozzle }
    : scope === 'layer' ? { layerHeight: ctx.layerHeight } : { nozzle: ctx.nozzle, layerHeight: ctx.layerHeight };
}
export function defaultScope(p: DynamicRecord, key: string, ctx: DynamicContext): DynamicScope {
  const scopes: DynamicScope[] = p.type === 'process' ? ['combo', 'nozzle', 'layer'] : ['nozzle'];
  return scopes.find((s) => p.dynamic?.layers.some((l) => key in l.values && sameWhen(l.when, scopeWhen(s, ctx)!))) ?? 'all';
}

const newLayerId = () => `layer:${crypto.randomUUID()}`;
export function withLayerValue(d: DynamicProfile, when: DynamicWhen, key: string, value: ConfigValue | undefined): DynamicProfile {
  const index = d.layers.findIndex((l) => sameWhen(l.when, when));
  const layer = index >= 0 ? d.layers[index] : { id: newLayerId(), when, values: {} };
  const values = { ...layer.values };
  if (value === undefined) delete values[key]; else values[key] = value;
  const layers = d.layers.filter((_, i) => i !== index);
  if (Object.keys(values).length) layers.splice(index >= 0 ? index : layers.length, 0, { ...layer, values });
  return { ...d, layers };
}

export const makeDynamic = <T extends DynamicRecord>(p: T, reference: { nozzle: number; layerHeight: number }): T =>
  ({ ...p, dynamic: { schema: 1, reference: { nozzle: round3(reference.nozzle), layerHeight: round3(reference.layerHeight) }, layers: [] } });

export interface FamilyMember { ctx: DynamicContext; config: Config }
const SKIP = /^(inherits|name|from|version|type|setting_id|base_id|filament_id|is_custom_defined|instantiation|\w+_settings_id|compatible_printers(_condition)?|compatible_prints(_condition)?|layer_height)$/;
export function deriveLayers(type: PresetType, reference: FamilyMember, members: FamilyMember[]): DynamicProfile {
  const rec: DynamicRecord & { dynamic: DynamicProfile } = { type, dynamic: { schema: 1,
    reference: { nozzle: round3(reference.ctx.nozzle), layerHeight: round3(reference.ctx.layerHeight) }, layers: [] } };
  const models = new Set(members.map((m) => m.ctx.printerModel).filter(Boolean));
  const keys = [...new Set(members.flatMap((m) => Object.keys(m.config)))].filter((k) => !SKIP.test(k)).sort();
  const resolved = (m: FamilyMember) => ({ ...reference.config, ...resolveDynamic(rec, m.ctx, reference.config) });
  for (const axis of ['printerModel', 'layerHeight', 'nozzle'] as Axis[]) {
    if (axis === 'layerHeight' && type !== 'process') continue;
    const values = [...new Set(members.map((m) => m.ctx[axis]))].filter((v) => v !== '' && v !== undefined)
      .sort((a, b) => String(a).localeCompare(String(b), undefined, { numeric: true }));
    if (values.length < 2) continue;
    const now = members.map(resolved);
    for (const v of values) {
      const group = members.map((m, i) => [m, now[i]] as const).filter(([m]) => axis === 'printerModel' ? m.ctx[axis] === v : sameMm(m.ctx[axis] as number, v as number));
      for (const key of keys) {
        const first = group[0][0].config[key];
        if (first === undefined || !group.every(([m]) => sameDynamicValue(m.config[key], first))) continue;
        if (group.every(([, r]) => sameDynamicValue(r[key], first))) continue;
        rec.dynamic = withLayerValue(rec.dynamic, { [axis]: v } as DynamicWhen, key, first);
      }
    }
  }
  for (const m of members) {
    const r = resolved(m);
    const when: DynamicWhen = type === 'process' ? { nozzle: m.ctx.nozzle, layerHeight: m.ctx.layerHeight } : { nozzle: m.ctx.nozzle };
    if (models.size > 1) when.printerModel = m.ctx.printerModel;
    for (const key of keys) if (m.config[key] !== undefined && !sameDynamicValue(r[key], m.config[key]))
      rec.dynamic = withLayerValue(rec.dynamic, when, key, m.config[key]);
  }
  return rec.dynamic;
}

export const qualityName = (presetName: string) => presetName.replace(/\s*@.*$/, '').replace(/^\d+(\.\d+)?\s*mm\s+/i, '').trim();
