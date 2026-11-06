



import type { ObjectResult } from './preview/explain-record';
import { applyStyle } from './print-style';
import { beginStep } from './state/history';
import { onShellsChecked } from './plater/geometry';
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

export interface PlateResult extends ObjectResult { objectId?: number; label: string }

const NONE: PlateResult[] = [];
let kept: { record: unknown; objects: unknown; rec: unknown; out: PlateResult[] } | null = null;





export function plateResults(s: State): PlateResult[] {
  const record = s.job.state === 'done' && !s.job.stale ? s.job.status?.record : undefined;
  if (kept && kept.record === record && kept.objects === s.objects && kept.rec === s.recommendedStyle) return kept.out;
  const warned = new Set(pendingRecommendations(s).map((o) => o.id));
  if (record && !explain) return NONE;
  const results = explain?.objectResults(record) ?? [];


  const bare = (n: string) => n.replace(/\.(stl|obj|3mf|step|stp)$/i, '');
  const out = results.flatMap((r) => {
    const o = s.objects.length === 1 && results.length === 1 ? s.objects[0] : s.objects.find((p) => bare(p.name) === bare(r.name));
    return o && warned.has(o.id) ? [] : [{ ...r, objectId: o?.id, label: o?.name ?? r.name }];
  });
  kept = { record, objects: s.objects, rec: s.recommendedStyle, out };
  return out;
}
