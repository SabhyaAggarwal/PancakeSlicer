import type { Config } from './api/types';
import type { ObjectResult } from './preview/explain-record';
import { applyStyle } from './print-style';
import { beginStep } from './state/history';
import { onShellsChecked, topSlopes } from './plater/geometry';
import { effectiveConfig, getState, setState, subscribeStore, type PlateObject, type State } from './state/store';
import { styleOf } from './style-names';

function refresh() {
  const s = getState();
  const next: State['recommendedStyle'] = {};
  for (const o of [...s.objects, ...s.projectPlates.flatMap((p) => p.objects)])
    if (o.geometry.userData.printInPlaceGap != null) next[o.id] = 'standard';
  const prev = s.recommendedStyle;
  const keys = Object.keys(next);
  if (keys.length !== Object.keys(prev).length || keys.some((k) => prev[+k] !== next[+k])) setState({ recommendedStyle: next });
}
onShellsChecked(refresh);
subscribeStore(refresh);

let explain: typeof import('./preview/explain-record') | null = null;
export const explainRecord = () => explain;
subscribeStore(() => {
  if (explain || getState().job.state !== 'done') return;
  void import('./preview/explain-record').then((m) => { if (!explain) { explain = m; kept = null; setState({}); } });
});

export function pendingRecommendations(s: State): PlateObject[] {
  const global = effectiveConfig(s, 'process');
  return s.objects.filter((o) => s.recommendedStyle[o.id] && styleOf({ ...global, ...o.settings }) !== s.recommendedStyle[o.id]);
}

export function applyRecommended(objects: PlateObject[]) {
  if (!objects.length) return;
  const end = beginStep('Change print style to "Standard"');
  try {
    setState({ processScope: 'objects', selected: { object: objects[0].id, instance: objects[0].instances[0].id } });
    for (const o of objects) applyStyle(getState().recommendedStyle[o.id], o.id);
  } finally {
    end();
  }
}

export interface PlateResult extends ObjectResult { objectId?: number; label: string; flat?: string }

const NONE: PlateResult[] = [];
let kept: { record: unknown; objects: unknown; rec: unknown; out: PlateResult[] } | null = null;
function flatReason(r: ObjectResult, o: PlateObject | undefined, process: Config): { why: string; hint?: string } | null {
  if (!r.limit) return null;
  const slopes = o && topSlopes(o), whole = (v: number) => String(Math.round(v));
  const slope = slopes && slopes.max >= 0.5 ? `Top slope ${slopes.max - slopes.min > 1 ? 'up to ' : ''}${whole(slopes.max)}°` : 'Top';
  const { kind, capDeg, reachableDeg } = r.limit;
  if (kind === 'access') return { why: reachableDeg != null ? `${slope} is steeper than the print head reaches (${whole(reachableDeg)}°)` : 'The print head cannot reach this top' };
  if (kind === 'angle') return { why: `${slope} is above your ${capDeg != null ? `${whole(capDeg)}° ` : ''}curved-top limit`, hint: 'try raising Max curved top angle' };
  if (kind === 'height') {
    const height = Number({ ...process, ...o?.settings }.nonplanar_curved_max_height);
    return { why: `Top rises more than your ${Number.isFinite(height) ? `${height} mm ` : ''}curved-top height limit` };
  }
  if (kind === 'area') return { why: 'Top too small for a curved top' };
  if (kind === 'base') return { why: 'Top too close to the bed for curved layers' };
  if (kind === 'budget') return { why: 'A curved top would cost too much time or quality' };
  return null;
}

function topFact(r: ObjectResult, o: PlateObject, process: Config): string {
  if (r.curved || r.ledge) return r.fact;
  const f = flatReason(r, o, process);
  return f ? `${f.why}, prints as Standard${f.hint ? `; ${f.hint}` : ''}` : r.fact;
}

function flatLine(r: ObjectResult, o: PlateObject | undefined, process: Config): string {
  const f = flatReason(r, o, process);
  return f ? `${f.why}${f.hint ? `; ${f.hint}` : ''}` : r.flatWhy || 'No top surface could be curved with the current limits';
}

export function plateResults(s: State): PlateResult[] {
  const record = s.job.state === 'done' && !s.job.stale ? s.job.status?.record : undefined;
  if (kept && kept.record === record && kept.objects === s.objects && kept.rec === s.recommendedStyle) return kept.out;
  const warned = new Set(pendingRecommendations(s).map((o) => o.id));
  if (record && !explain) return NONE;
  const results = explain?.objectResults(record) ?? [];
  const bare = (n: string) => n.replace(/\.(stl|obj|3mf|step|stp)$/i, '');
  const process = effectiveConfig(s, 'process');
  const out = results.flatMap((r) => {
    const o = s.objects.length === 1 && results.length === 1 ? s.objects[0] : s.objects.find((p) => bare(p.name) === bare(r.name));
    return o && warned.has(o.id) ? [] : [{ ...r, fact: o ? topFact(r, o, process) : r.fact, objectId: o?.id, label: o?.name ?? r.name, ...(r.printedFlat ? { flat: flatLine(r, o, process) } : {}) }];
  });
  kept = { record, objects: s.objects, rec: s.recommendedStyle, out };
  return out;
}
