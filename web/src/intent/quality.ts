





import type { SystemPreset } from '../api/types';
import { switchPreset } from '../actions';
import { beginStep } from '../state/history';
import { compatible, effectiveConfig, getState, resetOption, resolvePreset, setOption, type State } from '../state/store';

export type QualityId = 'draft' | 'standard' | 'fine';
export const QUALITY_IDS: QualityId[] = ['draft', 'standard', 'fine'];

const first = (v: unknown) => (Array.isArray(v) ? v[0] : v);
const num = (v: unknown) => parseFloat(String(first(v) ?? '')) || 0;


function nearest(list: SystemPreset[], lo: number, hi: number, target: number, prefer: RegExp) {
  return list.filter((p) => { const h = num(p.config.layer_height); return h > lo + 1e-6 && h <= hi + 1e-6; })
    .sort((a, b) => Number(prefer.test(b.name)) - Number(prefer.test(a.name))
      || Math.abs(num(a.config.layer_height) - target) - Math.abs(num(b.config.layer_height) - target))[0];
}


export interface Quality { preset: string; layer: number; edit?: string }

const cache = new WeakMap<SystemPreset[], Map<string, Record<QualityId, Quality | undefined>>>();
const r2 = (x: number) => Math.round(x * 100) / 100;


export function qualityPresets(s: State): Record<QualityId, Quality | undefined> {
  let byPrinter = cache.get(s.processes);
  if (!byPrinter) cache.set(s.processes, (byPrinter = new Map()));
  const hit = byPrinter.get(s.printer);
  if (hit) return hit;
  const machine = resolvePreset(s, 'machine', s.printer);
  const list = compatible(s, 'process', machine.base?.name ?? s.printer);
  const def = String(machine.config.default_print_profile ?? '');
  const standard = list.find((p) => p.name === def) ?? list.find((p) => /Standard/.test(p.name)) ?? list[0];
  const lh0 = num(standard?.config.layer_height) || 0.2;
  const nozzle = num(machine.config.nozzle_diameter) || 0.4;
  const real = (p: SystemPreset | undefined): Quality | undefined => p && { preset: p.name, layer: num(p.config.layer_height) };
  const derived = (lh: number): Quality | undefined => standard && Math.abs(lh - lh0) > 1e-6 ? { preset: standard.name, layer: lh, edit: String(lh) } : undefined;
  const out = {
    draft: real(nearest(list, lh0, nozzle * 0.75, lh0 * 1.2, /^(?!.*Extra).*Draft/)) ?? derived(Math.min(r2(lh0 * 1.2), r2(nozzle * 0.75))),
    standard: real(standard),
    fine: real(nearest(list, nozzle * 0.25 - 1e-6, lh0 * 0.75, lh0 * 0.6, /^(?!.*Extra).*Fine/)) ?? derived(Math.max(r2(lh0 * 0.6), r2(nozzle * 0.25))),
  };
  byPrinter.set(s.printer, out);
  return out;
}


export function currentQuality(s: State): QualityId | null {
  const q = qualityPresets(s);
  const base = resolvePreset(s, 'process', s.process).base?.name ?? s.process;
  const lh = num(effectiveConfig(s, 'process').layer_height);
  return QUALITY_IDS.find((id) => q[id]?.preset === base && Math.abs(q[id]!.layer - lh) < 1e-6) ?? null;
}


export function chooseQuality(id: QualityId) {
  const s = getState();
  const q = qualityPresets(s)[id];
  if (!q || currentQuality(s) === id) return;


  const surface = Object.fromEntries(Object.entries(effectiveConfig(s, 'process'))
    .filter(([key]) => /^(nonplanar_|zaa_)/.test(key)));
  const end = beginStep(`Quality: ${id}`);
  try {
    if (q.preset !== s.process) switchPreset('process', 0, q.preset, 'transfer');
    if (q.edit) setOption('process', 'layer_height', q.edit);
    else resetOption('process', 'layer_height');
    for (const [key, value] of Object.entries(surface)) setOption('process', key, value);
  } finally {
    end();
  }
}
