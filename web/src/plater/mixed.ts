import { effectiveConfig, getState, markStale, setState, subscribeStore, type Mix, type PlateObject, type State } from '../state/store';
import { undoable } from '../state/history';
import type { Config } from '../api/types';
import { filamentColor } from './filamentColor';
import { remapObject } from '../components/RemapFilaments';
import { blend } from './mixColour';

export const SLOTS_MAX = 32;
export { blend };

export const physicalCount = (s: State) => s.filamentSlots.length - Object.keys(s.mixes).length;
export const componentColour = (s: State, c: number) => (c >= 1 && c <= physicalCount(s) ? filamentColor(s, c) : '#808080');
export const mixColour = (s: State, m: Mix) => blend(m.components.map((c) => componentColour(s, c)), m.ratios);
export const filamentType = (s: State, c: number) => String([effectiveConfig(s, 'filament', c - 1).filament_type].flat()[0] ?? '');

export function broken(s: State, m: Mix) {
  const n = physicalCount(s);
  return m.components.length < 2 || m.components.some((c) => c < 1 || c > n) || new Set(m.components.map((c) => filamentType(s, c))).size > 1;
}

function remapAll(st: State, map: number[]): Partial<State> {
  const each = (os: PlateObject[]) => os.map((o) => remapObject(o, map));
  const events = (es: State['events']) => es.flatMap((e) => (e.type !== 'change' ? [e] : map[Number(e.text) - 1] ? [{ ...e, text: String(map[Number(e.text) - 1]) }] : []));
  return {
    objects: each(st.objects), events: events(st.events),
    projectPlates: st.projectPlates.map((p) => (p.index === st.plateIndex ? p : { ...p, objects: each(p.objects), events: p.events && events(p.events) })),
    job: markStale(st.job),
  };
}

const shifted = (mixes: Record<number, Mix>, from: number, by: number) =>
  Object.fromEntries(Object.entries(mixes).map(([i, m]) => [Number(i) >= from ? Number(i) + by : Number(i), m]));

export function addMix(m: Mix) {
  undoable('Add mixed filament', () => setState((st) => {
    const i = st.filamentSlots.length, slotColours = [...st.slotColours];
    slotColours[i] = mixColour(st, m);
    return { filamentSlots: [...st.filamentSlots, st.filamentSlots[m.components[0] - 1] ?? st.filamentSlots[0]], slotColours, mixes: { ...st.mixes, [i]: m }, job: markStale(st.job) };
  }));
}

export function editMix(i: number, m: Mix) {
  undoable('Edit mixed filament', () => setState((st) => {
    const slotColours = [...st.slotColours];
    slotColours[i] = mixColour(st, m);
    return { slotColours, mixes: { ...st.mixes, [i]: m }, job: markStale(st.job) };
  }));
}

export function deleteMixes(slots: number[], into?: number, name = 'Delete mixed filament') {
  undoable(name, () => setState((st) => {
    const gone = new Set(slots), n = st.filamentSlots.length;
    const kept = Array.from({ length: n }, (_, i) => i).filter((i) => !gone.has(i));
    const id = (k: number) => kept.indexOf(k) + 1;
    const map = Array.from({ length: n }, (_, k) => (gone.has(k) ? (into ? id(into - 1) : 1) : id(k)));
    return {
      ...remapAll(st, map),
      filamentSlots: kept.map((k) => st.filamentSlots[k]), slotColours: kept.map((k) => st.slotColours[k]),
      mixes: Object.fromEntries(kept.flatMap((k, j) => (st.mixes[k] ? [[j, st.mixes[k]]] : []))),
    };
  }));
}

export function addPhysical(colour?: string) {
  undoable('Add filament', () => setState((st) => {
    const p = physicalCount(st), n = st.filamentSlots.length;
    const filamentSlots = [...st.filamentSlots], slotColours = [...st.slotColours];
    while (slotColours.length < n) slotColours.push(undefined);
    filamentSlots.splice(p, 0, st.filamentSlots[p - 1]);
    slotColours.splice(p, 0, colour);
    return { ...remapAll(st, Array.from({ length: n }, (_, k) => (k >= p ? k + 2 : k + 1))), filamentSlots, slotColours, mixes: shifted(st.mixes, p, 1) };
  }));
}

export function removePhysical() {
  undoable('Remove filament', () => setState((st) => {
    const p = physicalCount(st), n = st.filamentSlots.length;
    if (p <= 1) return {};
    const filamentSlots = [...st.filamentSlots], slotColours = [...st.slotColours];
    filamentSlots.splice(p - 1, 1);
    slotColours.splice(p - 1, 1);
    const mixes = Object.fromEntries(Object.entries(shifted(st.mixes, p, -1)).map(([i, m]) =>
      [i, { ...m, components: m.components.map((c) => (c === p ? 0 : c > p ? c - 1 : c)) }]));
    return { ...remapAll(st, Array.from({ length: n }, (_, k) => (k === p - 1 ? 1 : k >= p ? k : k + 1))), filamentSlots, slotColours, mixes };
  }));
}

let watching = false;
export function watchColours() {
  if (watching) return;
  watching = true;
  subscribeStore(() => {
    const s = getState();
    const changed = Object.entries(s.mixes).filter(([i, m]) => s.slotColours[Number(i)] !== mixColour(s, m));
    if (!changed.length) return;
    const slotColours = [...s.slotColours];
    for (const [i, m] of changed) slotColours[Number(i)] = mixColour(s, m);
    setState({ slotColours });
  });
}

export function recommendations(s: State, three: boolean): Mix[] {
  const byType = new Map<string, number[]>();
  for (let c = 1; c <= physicalCount(s); c++) {
    const t = filamentType(s, c);
    if (!t.endsWith('-S')) byType.set(t, [...(byType.get(t) ?? []), c]);
  }
  const out: Mix[] = [];
  for (const ids of byType.values())
    for (let a = 0; a < ids.length; a++)
      for (let b = a + 1; b < ids.length; b++) {
        if (!three) out.push({ components: [ids[a], ids[b]], ratios: [50, 50], gradient: null, perPart: false });
        else for (let c = b + 1; c < ids.length; c++)
          for (let k = 0; k < 3; k++) out.push({ components: [ids[a], ids[b], ids[c]], ratios: [0, 1, 2].map((j) => (j === k ? 50 : 25)), gradient: null, perPart: false });
      }
  return out.slice(0, 100);
}

export function readMixes(c: Config): Record<number, Mix> {
  const at = (k: string, i: number) => { const v = c[k]; return String(Array.isArray(v) ? v[i] ?? '' : ''); };
  const on = (k: string, i: number) => ['1', 'true'].includes(at(k, i));
  const mixes: Record<number, Mix> = {};
  const n = Array.isArray(c.filament_is_mixed) ? c.filament_is_mixed.length : 0;
  for (let i = 0; i < n; i++) if (on('filament_is_mixed', i)) {
    const components = at('filament_mixed_components', i).split(',').filter(Boolean).map(Number);
    let ratios = at('filament_mixed_sublayer_ratios', i).split(',').filter(Boolean).map((r) => Math.trunc(Number(r) * 100 + 0.5));
    if (ratios.length !== components.length) ratios = components.map((_, k) => Math.trunc(100 / components.length) + (k ? 0 : 100 % components.length));
    const gradient = on('filament_mixed_gradient', i) ? (at('filament_mixed_gradient_range', i).startsWith('0.1') ? 1 : 0) : null;
    mixes[i] = { components, ratios, gradient, perPart: on('filament_mixed_gradient_per_part', i) };
  }
  return mixes;
}
