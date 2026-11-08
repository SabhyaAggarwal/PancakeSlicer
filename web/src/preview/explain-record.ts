import { recordConfig, styleName } from '../style-names';

type Rec = Record<string, unknown>;

const MIN_FLAT_ROW_MM2 = 2;
interface Patch { kind?: string; area_mm2?: number | null; shells?: number | null; reason_tags?: string[]; object_id?: number }
interface CurvedLayers { source?: string; layers?: number; refused?: string }


const FLAT_NAMES: Record<string, string> = {
  steep: 'Steep area', steep_strip: 'Steep strip', narrow: 'Narrow groove', head: 'Head clearance', flat: 'Flat area',
  edge: 'Edge', near_bed: 'Near bed', outline: 'Ragged outline', bottom: 'Underside',
};

const REASONS: Record<string, string> = {
  steep: 'too steep for the nozzle here',
  steep_strip: 'too steep for the nozzle here',
  narrow: 'too narrow',
  head: 'printhead would bump the part here',
  flat: 'already flat',
  edge: 'too close to an edge',
  near_bed: 'too close to the bed',
  outline: 'outline is too jagged to curve cleanly',
  bottom: 'it faces down',
};
export const reasonText = (tag: string) => REASONS[tag] ?? 'kept flat';
const overhangTag = (tags: string[]) => tags.some((t) => t.startsWith('overhang') || t === 'bottom');

export interface FlatArea { name: string; reason: string; area: number }

export interface RecordSummary {
  mode: string;
  style: string;
  curved: boolean;
  part: string;
  topCurved: number;
  topFlat: number;
  shells: number;
  overhangs: number;
  bridges: number;
  flat: FlatArea[];
  wholePart: { source: string; layers?: number } | null;
  refusedWhy: string | null;
  verifier: { ran: boolean; hits: number; closest: number | null; uncalibrated: boolean } | null;
  topErrorMm: number | null;
  timeS: number | null;
  grams: number | null;
}

const num = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? v : typeof v === 'string' && v.trim() && Number.isFinite(Number(v)) ? Number(v) : null);

export function readRecord(record: Rec | undefined): RecordSummary | null {
  if (!record || record.status === 'error') return null;
  const inputs = (record.inputs ?? {}) as Rec;
  const totals = (record.totals ?? {}) as Rec;
  const text = (record.text_summary ?? {}) as Rec;
  const objects = (record.objects ?? []) as { name?: string; mode?: string; deform?: string; curved_layers?: CurvedLayers }[];
  const patches = (((record.results ?? {}) as Rec).patches ?? []) as Patch[];
  const styleKeys = recordConfig(inputs, objects);
  const mode = String(styleKeys.nonplanar_mode);
  const style = styleName(styleKeys);
  const accepted = patches.filter((p) => p.kind !== 'fallback');
  const fallback = patches.filter((p) => p.kind === 'fallback');
  const curvedTime = (num(text.nonplanar_extrude_time_s) ?? 0) + (num(text.nonplanar_other_time_s) ?? 0);
  const timed = text.nonplanar_extrude_time_s !== undefined;
  const sum = (ps: Patch[]) => ps.reduce((a, p) => a + (num(p.area_mm2) ?? 0), 0);
  const flatByTag = new Map<string, number>();
  for (const p of fallback) {
    const tags = p.reason_tags?.length ? p.reason_tags : ['untagged'];
    if (overhangTag(tags)) continue;
    if (!((num(p.area_mm2) ?? 0) >= 0.05)) continue;
    flatByTag.set(tags[0], (flatByTag.get(tags[0]) ?? 0) + (num(p.area_mm2) ?? 0));
  }
  const top = accepted.filter((p) => p.kind === 'top');
  const cl = objects.map((o) => o.curved_layers).find((c) => c && typeof c === 'object');
  const volumeOnly = accepted.length > 0 && accepted.every((p) => p.kind === 'volume' && num(p.area_mm2) == null);
  const wholePart = cl && !cl.refused && (cl.layers ?? 1) > 0 ? { source: String(cl.source ?? style), layers: cl.layers }
    : !cl && volumeOnly ? { source: style } : null;
  const v = totals.verifier as Rec | undefined;
  const names = [...new Set(objects.map((o) => String(o.name ?? '').replace(/\.[^.]+$/, '')).filter(Boolean))];
  const stats = record.statistics as Rec | undefined;
  return {
    mode,
    style,
    curved: timed ? curvedTime > 0 : accepted.length > 0,
    part: names.length === 1 ? `your ${names[0]}` : 'your parts',
    topCurved: sum(top),
    topFlat: [...flatByTag.values()].reduce((a, b) => a + b, 0),
    shells: top.reduce((m, p) => Math.max(m, num(p.shells) ?? 0), 0),
    overhangs: accepted.filter((p) => p.kind === 'overhang').length,
    bridges: accepted.filter((p) => p.kind === 'bridge').length,
    flat: [...flatByTag].filter(([, area]) => area >= MIN_FLAT_ROW_MM2)
      .map(([tag, area]) => ({ name: FLAT_NAMES[tag] ?? 'Kept flat', reason: reasonText(tag), area })).sort((a, b) => b.area - a.area),
    wholePart,
    refusedWhy: cl?.refused ? String(cl.refused) : null,
    verifier: v && v.ran ? {
      ran: true,
      hits: num(v.hit_moves) ?? 0,
      closest: num(v.min_clearance_mm),
      uncalibrated: ((v.omissions ?? []) as string[]).some((o) => /not been calibrated|uncalibrated/i.test(o)),
    } : null,
    topErrorMm: num(totals.whole_top_rms_mm),
    timeS: num(totals.estimated_time_s) ?? num(text.estimated_time_s),
    grams: num(stats?.total_weight_g),
  };
}

export const microns = (mm: number | null) => (mm == null ? '–' : `${Math.round(mm * 1000)} µm`);
export const grams = (g: number | null) => (g == null ? '–' : `${g.toFixed(2)} g`);
export const supportText = (g: number | null) => (g == null ? '–' : g > 0 ? `${g.toFixed(2)} g` : '0 g');
export const area = (mm2: number) => `${mm2 < 10 ? mm2.toFixed(1) : Math.round(mm2)} mm²`;

export type PlanarLimit = 'access' | 'angle' | 'height' | 'area' | 'budget' | 'base';
export interface ObjectResult { name: string; style: string; fact: string; curved: boolean; ledge: boolean; pct: number | null;
  topMm2: number;
  limit?: { kind: PlanarLimit; capDeg: number | null; reachableDeg: number | null } }

export function objectResults(record: Rec | undefined): ObjectResult[] {
  if (!record || record.status === 'error') return [];
  const inputs = (record.inputs ?? {}) as Rec;
  const base = recordConfig(inputs);
  const patches = (((record.results ?? {}) as Rec).patches ?? []) as Patch[];
  const objects = (record.objects ?? []) as { id?: number; name?: string; mode?: string; deform?: string; curved_layers?: CurvedLayers }[];
  const reported = (((record.nonplanar_report ?? {}) as Rec).objects ?? []) as { name?: string; patches?: Patch[]; curved_top_area_fraction?: number;
    planar_limit?: PlanarLimit; curved_max_angle_deg?: number; reachable_slope_deg?: number }[];
  const byName = new Map<string, { ids: Set<number>; mode: string; deform?: string; cl?: CurvedLayers }>();
  for (const o of objects) {
    const name = String(o.name ?? '');
    const e = byName.get(name) ?? { ids: new Set<number>(), mode: String(o.mode ?? inputs.mode ?? 'off'), deform: o.deform, cl: o.curved_layers };
    if (o.id != null) e.ids.add(o.id);
    byName.set(name, e);
  }
  const out: ObjectResult[] = [];
  for (const [name, o] of byName) {
    if (o.mode === 'off') continue;
    const style = styleName({ ...base, nonplanar_mode: o.mode, ...(o.deform ? { nonplanar_deform: o.deform } : {}) });
    let mine = patches.filter((p) => p.object_id != null && o.ids.has(p.object_id));
    const native = mine.length ? undefined : reported.find((r) => r.name === name);
    if (native) mine = native.patches ?? [];
    const footprints = (kinds: string[]) => {
      const seen = new Set<number>();
      let total = 0;
      for (const p of mine) {
        const a = num(p.area_mm2) ?? 0;
        if (!kinds.includes(String(p.kind)) || a <= 0 || seen.has(Math.round(a * 100))) continue;
        seen.add(Math.round(a * 100));
        total += a;
      }
      return total;
    };
    const top = mine.filter((p) => p.kind === 'top').reduce((a, p) => a + (num(p.area_mm2) ?? 0), 0);
    const ledge = footprints(['overhang', 'bridge']);
    const flat = mine.filter((p) => p.kind === 'fallback' && !overhangTag(p.reason_tags?.length ? p.reason_tags : ['untagged']) && (num(p.area_mm2) ?? 0) >= 0.05)
      .reduce((a, p) => a + (num(p.area_mm2) ?? 0), 0);
    const share = native && Number.isFinite(native.curved_top_area_fraction) ? native.curved_top_area_fraction! : top / (top + flat);
    const pct = top >= 1 ? Math.min(flat > 0 || share < 1 ? 99 : 100, Math.max(1, Math.round(100 * share))) : 0;
    let fact: string;
    if (o.cl && !o.cl.refused && (o.cl.layers ?? 1) > 0) fact = 'Whole part in curved layers';
    else if (ledge >= 1 && ledge > top) fact = 'No support under ledge';
    else if (top >= 1) fact = `Curved top ${pct}%`;
    else fact = 'Flat top, prints as Standard';
    const why = reported.find((r) => r.name === name);
    const limit = why?.planar_limit ? { kind: why.planar_limit, capDeg: num(why.curved_max_angle_deg), reachableDeg: num(why.reachable_slope_deg) } : undefined;
    out.push({ name, style, fact, curved: !fact.startsWith('Flat'), ledge: ledge >= 1, pct, topMm2: top + flat, ...(limit ? { limit } : {}) });
  }
  return out;
}

const FALLBACK_REASONS: Record<string, [string, string]> = {
  flat: ['flat', 'flat: planar layers print it exactly'],
  planar_patch: ['flat', 'tilted plane: planar layers, Z contoured'],
  flat_ground: ['flat', 'flat ground: planar layers, Z contoured, print it exactly'],
  flat_sliver: ['flat', 'a sliver beside flat ground, printed with it'],
  steep: ['wall', 'steeper than a curved bead follows (atan(h/w)): planar layers, Z contoured'],
  steep_strip: ['wall', 'a steep strip between curved tops: planar layers, Z contoured'],
  narrow: ['wall', 'narrower than half a road: the contoured planar top measured better'],
  outline: ['outline', 'outline between surface samples'],
  near_bed: ['fallback', 'too close to the first layer for a shell'],
  no_transition_layer: ['fallback', 'no planar layer under the first shell within the bead range'],
  no_pieces: ['fallback', 'the bands laid no road there'],
  shell_height: ['unreachable', 'shell layer too thick for the nozzle'],
  edge: ['fallback', 'edge of a curved top: near the bed, too steep or narrower than a road'],
  head: ['fallback', 'the print head cannot reach it'],
  untagged: ['fallback', 'no reason recorded'],
};

export interface ReportObject {
  name: string;
  mode: string;
  topPatches: number;
  otherPatches: number;
  bands: number;
  curvedTop: number;
  planarTop: number;
  flatTop: number;
  planarReason: string;
  flatReason: string;
  share?: number;
  supportFree?: number;
  orientation?: string;
}

type ReportFallbackJson = { reason?: string; class?: string; why?: string; area_mm2?: number; top?: boolean };

function fromReportJson(r: Rec) {
  const clearance = (r.clearance ?? {}) as Rec;
  const objects = ((r.objects ?? []) as Rec[]).map((o): ReportObject => {
    const mainReason = (fallback: boolean) => {
      const by = new Map<string, [number, string]>();
      for (const f of (o.fallbacks ?? []) as ReportFallbackJson[]) {
        if (!f.top || (f.class === 'fallback') !== fallback) continue;
        const tag = f.reason ?? '', prev = by.get(tag);
        by.set(tag, [(prev?.[0] ?? 0) + (num(f.area_mm2) ?? 0), f.why || FALLBACK_REASONS[tag]?.[1] || tag]);
      }
      return [...by.values()].sort((a, b) => b[0] - a[0])[0]?.[1] ?? '-';
    };
    return {
      name: String(o.name ?? ''), mode: String(o.mode ?? 'off'), topPatches: num(o.top_patches) ?? 0,
      otherPatches: num(o.other_patches) ?? 0, bands: num(o.bands) ?? 0, curvedTop: num(o.curved_top_mm2) ?? 0,
      planarTop: num(o.planar_top_mm2) ?? 0, flatTop: num(o.flat_top_mm2) ?? 0,
      planarReason: mainReason(true), flatReason: mainReason(false), share: num(o.curved_share) ?? undefined,
      supportFree: num(o.support_free_mm2) ?? 0, orientation: String(o.orientation ?? ''),
    };
  });
  return {
    head: String(r.head ?? ''), verified: !!clearance.checked, hits: num(clearance.hits) ?? 0,
    timeS: num(r.time_s) ?? 0, curvedS: num(r.curved_time_s) ?? 0, objects,
  };
}

export function curvedTopPercent(record: Rec | undefined): number | null {
  const tops = objectResults(record).filter((r) => r.pct != null && r.topMm2 >= 1);
  const total = tops.reduce((a, r) => a + r.topMm2, 0);
  if (!total) return null;
  const share = tops.reduce((a, r) => a + r.pct! * r.topMm2, 0) / total;
  return Math.min(tops.some((r) => r.pct! < 100) ? 99 : 100, Math.max(1, Math.round(share)));
}

export function readReport(record: Rec | undefined) {
  if (!record || record.status !== 'sliced') return null;
  const report = record.nonplanar_report as Rec | undefined;
  if (report && typeof report === 'object') {
    const own = (report.objects ?? []) as { patches?: unknown[]; fallbacks?: unknown[] }[];
    return own.some((o) => o.patches?.length || o.fallbacks?.length) ? fromReportJson(report) : null;
  }
  const objects = (record.objects ?? []) as { id?: number; name?: string; mode?: string }[];
  if (!objects.some((o) => o.mode && o.mode !== 'off')) return null;
  const patches = (((record.results ?? {}) as Rec).patches ?? []) as (Patch & { band?: number | null })[];
  const text = (record.text_summary ?? {}) as Rec;
  const v = ((record.totals ?? {}) as Rec).verifier as Rec | undefined;
  const mainReason = (areas: Map<string, number>) => {
    const tag = [...areas].sort((a, b) => b[1] - a[1])[0]?.[0];
    return tag === undefined ? '-' : FALLBACK_REASONS[tag]?.[1] ?? tag;
  };
  const out: ReportObject[] = objects.map((o, i) => {
    const own = patches.filter((p) => (p.object_id ?? 0) === (o.id ?? i));
    const top = own.filter((p) => p.kind === 'top');
    const planar = new Map<string, number>(), flat = new Map<string, number>();
    let planarTop = 0, flatTop = 0;
    for (const p of own.filter((q) => q.kind === 'fallback')) {
      const reason = p.reason_tags?.[0] ?? 'untagged', area = num(p.area_mm2) ?? 0;
      if (!(area > 0) || reason === 'bottom' || reason.startsWith('overhang')) continue;
      const exact = (FALLBACK_REASONS[reason]?.[0] ?? 'fallback') !== 'fallback';
      const by = exact ? flat : planar;
      by.set(reason, (by.get(reason) ?? 0) + area);
      if (exact) flatTop += area; else planarTop += area;
    }
    return {
      name: String(o.name ?? ''), mode: String(o.mode ?? 'off'), topPatches: top.length,
      otherPatches: own.filter((p) => p.kind !== 'top' && p.kind !== 'fallback').length,
      bands: top.reduce((m, p) => Math.max(m, (num(p.band) ?? -1) + 1), 0),
      curvedTop: top.reduce((a, p) => a + (num(p.area_mm2) ?? 0), 0), planarTop, flatTop,
      planarReason: mainReason(planar), flatReason: mainReason(flat),
    };
  });
  return {
    head: String(((record.inputs ?? {}) as Rec & { head?: { id?: string } }).head?.id ?? ''),
    verified: !!v?.ran,
    hits: num(v?.hit_moves) ?? 0,
    timeS: num(text.estimated_time_s) ?? 0,
    curvedS: (num(text.nonplanar_extrude_time_s) ?? 0) + (num(text.nonplanar_other_time_s) ?? 0),
    objects: out,
  };
}

export function droppedCurvedTop(record: Rec | undefined): number | null {
  type Budget = { measured?: boolean; planar_fw_s?: number | null; patches?: { dropped?: boolean; time_cost_s?: number | null }[] };
  const objects = (((record?.nonplanar_report ?? {}) as Rec).objects ?? []) as { curved_top_budget?: Budget }[];
  let planar = 0, cost = 0;
  for (const { curved_top_budget: b } of objects) {
    const dropped = b?.measured ? (b.patches ?? []).filter((p) => p.dropped && Number.isFinite(Number(p.time_cost_s))) : [];
    if (!dropped.length || !(Number(b!.planar_fw_s) > 0)) continue;
    planar += Number(b!.planar_fw_s);
    cost += dropped.reduce((sum, p) => sum + Math.max(0, Number(p.time_cost_s)), 0);
  }
  return planar > 0 ? Math.max(1, Math.round((100 * cost) / planar)) : null;
}

export function nativeSurfaceSummary(record: Rec): string | null {
  const report = record.nonplanar_report as { objects?: {
    mode?: string; curved_top_area_fraction?: number; curved_top_area_denominator_mm2?: number;
    curved_extrusion_length_mm?: number; planar_fallback_reason?: string;
  }[] } | undefined;
  const objects = report?.objects?.filter((o) => o.mode === 'curved_tops');
  if (!objects?.length) {
    const why = report?.objects?.find((o) => o.mode === 'conformal_layers' && o.planar_fallback_reason)?.planar_fallback_reason;
    return why ? `Printed flat: ${why}` : null;
  }
  const roads = objects.reduce((sum, o) => sum + (Number(o.curved_extrusion_length_mm) || 0), 0);
  if (roads <= 0) return `Printed flat: ${objects.map((o) => o.planar_fallback_reason).find(Boolean) || 'No top surfaces could be curved with the current clearance limits'}`;
  const valid = objects.every((o) => Number.isFinite(o.curved_top_area_fraction));
  const area = objects.reduce((sum, o) => sum + (Number(o.curved_top_area_denominator_mm2) || 0), 0);
  const fraction = !valid ? null : objects.length === 1 ? objects[0].curved_top_area_fraction!
    : area > 0 && objects.every((o) => Number(o.curved_top_area_denominator_mm2) > 0)
      ? objects.reduce((sum, o) => sum + o.curved_top_area_fraction! * o.curved_top_area_denominator_mm2!, 0) / area : null;
  const coverage = fraction != null && fraction >= 0 && fraction <= 1 ? `${(fraction * 100).toFixed(1)} % of top surface curved` : 'Curved top surfaces';
  return coverage;
}
