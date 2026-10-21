import { recordConfig, STYLE_NAMES, styleName } from '../style-names';

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
  head: 'the print head would touch the part',
  flat: 'already flat',
  edge: 'too close to an edge',
  near_bed: 'too close to the bed',
  outline: 'its outline is too ragged',
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

export interface ObjectResult { name: string; style: string; fact: string; curved: boolean; ledge: boolean; pct: number | null }

function oneshotFact(record: Rec): { fact: string; curved: boolean } | null {
  const decision = String(((record.oneshot ?? {}) as Rec).decision ?? '');
  if (!decision) return null;
  const cpu = num(((record.totals ?? {}) as Rec).cpu_seconds);
  const slice = cpu == null ? '' : ` · Slice ${Math.round(cpu)} s CPU`;
  const fell = /fell back to planar, (.*)$/.exec(decision);
  if (fell) return { fact: `Planar fallback: ${fell[1]}${slice}`, curved: false };
  const ratio = /= ([\d.]+)x planar/.exec(decision)?.[1];
  return { fact: `${decision.includes('z-only') ? 'Flat' : 'Curved'} layers${ratio ? `, ${ratio}× planar time` : ''}${slice}`, curved: true };
}

export function objectResults(record: Rec | undefined): ObjectResult[] {
  if (!record || record.status === 'error') return [];
  const inputs = (record.inputs ?? {}) as Rec;
  const oneshot = oneshotFact(record);
  if (oneshot) {
    const names = [...new Set(((record.objects ?? []) as { name?: string }[]).map((o) => String(o.name ?? '')))];
    return names.map((name) => ({ name, style: STYLE_NAMES.oneshot, ...oneshot, ledge: false, pct: null }));
  }
  const base = recordConfig(inputs);
  const patches = (((record.results ?? {}) as Rec).patches ?? []) as Patch[];
  const objects = (record.objects ?? []) as { id?: number; name?: string; mode?: string; deform?: string; curved_layers?: CurvedLayers }[];
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
    const mine = patches.filter((p) => p.object_id != null && o.ids.has(p.object_id));
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
    const pct = top >= 1 ? Math.min(flat > 0 ? 99 : 100, Math.max(1, Math.round((100 * top) / (top + flat)))) : 0;
    let fact: string;
    if (o.cl && !o.cl.refused && (o.cl.layers ?? 1) > 0) fact = 'Whole part in curved layers';
    else if (ledge >= 1 && ledge > top) fact = 'No support under ledge';
    else if (top >= 1) fact = `Curved top ${pct}%`;
    else fact = 'Flat top, prints as Standard';
    out.push({ name, style, fact, curved: !fact.startsWith('Flat'), ledge: ledge >= 1, pct });
  }
  return out;
}
