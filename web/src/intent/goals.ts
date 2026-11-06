





import type { Config, ConfigValue, SystemPreset } from '../api/types';
import { toolValue } from '../config-values';
import { OPTIONS } from '../options';
import { undoable } from '../state/history';
import { compatible, getState, PRESET_META, resolvePreset, setState, subscribeStore, type State } from '../state/store';

export type GoalId = 'balanced' | 'strong' | 'fine' | 'fast';
export const GOAL_IDS: GoalId[] = ['balanced', 'strong', 'fine', 'fast'];
export const GOAL_NAMES: Record<GoalId, string> = { balanced: 'Balanced', strong: 'Strong', fine: 'Fine', fast: 'Fast' };


export interface Delta { key: string; from: ConfigValue; to: ConfigValue; source: string }
export interface Goal { id: GoalId; name: string; deltas: Delta[] }
export interface Goals { base: string; goals: Record<GoalId, Goal> }

export interface GoalMatch { id: GoalId; edits: string[]; goals: Goals }

const isStyleKey = (k: string) => /^(nonplanar_|zaa_)/.test(k);

const IGNORED = /^(compatible_printers(_condition)?|compatible_prints(_condition)?|print_settings_id|inherits|default_\w+|print_compatible_printers|filament_settings_id|printer_settings_id|notes|setting_id)$/;
const counts = (k: string) => !isStyleKey(k) && !PRESET_META.test(k) && !IGNORED.test(k);

const first = (v: ConfigValue | undefined) => (Array.isArray(v) ? v[0] : v);
export const num = (v: ConfigValue | undefined) => parseFloat(String(first(v) ?? '')) || 0;
const r2 = (x: number) => Math.round(x * 100) / 100;
const dflt = (k: string) => (OPTIONS[k]?.default as ConfigValue | undefined) ?? '';
export const same = (k: string, a: ConfigValue | undefined, b: ConfigValue | undefined) => toolValue(k, a ?? dflt(k)) === toolValue(k, b ?? dflt(k));



const FROM_PRESET: Record<Exclude<GoalId, 'balanced'>, string[]> = {
  strong: ['wall_loops', 'sparse_infill_density', 'sparse_infill_pattern', 'top_shell_layers', 'bottom_shell_layers'],
  fine: ['layer_height', 'top_shell_layers', 'bottom_shell_layers'],
  fast: ['layer_height', 'top_shell_layers', 'bottom_shell_layers'],
};


function baseProcess(s: State, list: SystemPreset[]): string {
  const machine = resolvePreset(s, 'machine', s.printer).base;
  const def = String(machine?.config.default_print_profile ?? '');
  if (list.some((p) => p.name === def)) return def;
  return resolvePreset(s, 'process', s.process).base?.name ?? s.process;
}


function nearest(list: SystemPreset[], lo: number, hi: number, target: number, prefer: RegExp) {
  return list.filter((p) => { const h = num(p.config.layer_height); return h > lo + 1e-6 && h <= hi + 1e-6; })
    .sort((a, b) => Number(prefer.test(b.name)) - Number(prefer.test(a.name)) || Math.abs(num(a.config.layer_height) - target) - Math.abs(num(b.config.layer_height) - target)
      || num(b.config.layer_height) - num(a.config.layer_height))[0];
}

function fromPreset(id: Exclude<GoalId, 'balanced'>, base: Config, p: SystemPreset | undefined): Delta[] {
  if (!p) return [];
  return FROM_PRESET[id].filter((k) => p.config[k] !== undefined && !same(k, p.config[k], base[k]))
    .map((k) => ({ key: k, from: base[k] ?? dflt(k), to: p.config[k], source: p.name }));
}


function shells(base: Config, lh: number, source: string): Delta[] {
  const lh0 = num(base.layer_height) || 0.2;
  const out: Delta[] = [];
  for (const [k, t] of [['top_shell_layers', 'top_shell_thickness'], ['bottom_shell_layers', 'bottom_shell_thickness']] as const) {
    const n = Math.max(Math.ceil(Math.max(num(base[k]) * lh0, num(base[t])) / lh - 1e-6), 2);
    if (n !== num(base[k])) out.push({ key: k, from: base[k] ?? dflt(k), to: String(n), source });
  }
  return out;
}

const RULE = 'rule';
const cache = new WeakMap<SystemPreset[], Map<string, Goals>>();


export function deriveGoals(s: State): Goals {
  const list = compatible(s, 'process', resolvePreset(s, 'machine', s.printer).base?.name ?? s.printer);
  let byPrinter = cache.get(s.processes);
  if (!byPrinter) cache.set(s.processes, (byPrinter = new Map()));
  const hit = byPrinter.get(s.printer);
  if (hit) return hit;
  const baseName = baseProcess(s, list);
  const base = resolvePreset(s, 'process', baseName).config;
  const lh0 = num(base.layer_height) || 0.2;
  const nozzle = num(resolvePreset(s, 'machine', s.printer).config.nozzle_diameter) || 0.4;

  const fineP = nearest(list, nozzle * 0.25 - 1e-6, lh0 * 0.75, lh0 * 0.6, /Fine|Optimal/);
  let fine = fromPreset('fine', base, fineP);
  if (!fine.some((d) => d.key === 'layer_height')) {
    const lh = Math.max(r2(lh0 * 0.6), r2(nozzle * 0.25));
    fine = [{ key: 'layer_height', from: base.layer_height ?? dflt('layer_height'), to: String(lh), source: RULE }, ...shells(base, lh, RULE)];
  }

  const fastP = nearest(list, lh0, nozzle * 0.75, lh0 * 1.4, /Draft/);
  let fast = fromPreset('fast', base, fastP);
  if (!fast.some((d) => d.key === 'layer_height')) {
    const lh = Math.min(r2(lh0 * 1.4), r2(nozzle * 0.7));
    fast = lh > lh0 ? [{ key: 'layer_height', from: base.layer_height ?? dflt('layer_height'), to: String(lh), source: RULE }, ...shells(base, lh, RULE)] : [];
  }

  if (num(base.sparse_infill_density) > 10) fast.push({ key: 'sparse_infill_density', from: base.sparse_infill_density, to: '10%', source: RULE });

  const strongP = list.find((p) => /Strength/.test(p.name) && Math.abs(num(p.config.layer_height) - lh0) < 1e-6);
  let strong = fromPreset('strong', base, strongP);
  if (!strong.some((d) => d.key === 'wall_loops')) {
    const walls = num(base.wall_loops) || 2;
    strong = [{ key: 'wall_loops', from: base.wall_loops ?? dflt('wall_loops'), to: String(Math.max(walls + 2, 4)), source: RULE }];
    if (num(base.sparse_infill_density) < 25) strong.push({ key: 'sparse_infill_density', from: base.sparse_infill_density ?? dflt('sparse_infill_density'), to: '25%', source: RULE });
    strong.push({ key: 'top_shell_layers', from: base.top_shell_layers ?? dflt('top_shell_layers'), to: String((num(base.top_shell_layers) || 3) + 1), source: RULE });
  }

  const goal = (id: GoalId, deltas: Delta[]): Goal => ({ id, name: GOAL_NAMES[id], deltas });
  const out: Goals = { base: baseName, goals: { balanced: goal('balanced', []), strong: goal('strong', strong), fine: goal('fine', fine), fast: goal('fast', fast) } };
  byPrinter.set(s.printer, out);
  return out;
}


function goalEdits(s: State, g: Goal): Config {
  const keep = Object.fromEntries(Object.entries(s.dirty.process).filter(([k]) => isStyleKey(k)));
  for (const d of g.deltas) keep[d.key] = d.to;
  return keep;
}

let matchKey: unknown[] = [];
let matchOut: GoalMatch | null = null;

export function goalMatch(s: State): GoalMatch {
  const plate = s.projectPlates.find((p) => p.index === s.plateIndex)?.settings;
  const key = [s.processes, s.printer, s.process, s.dirty.process, s.projectPresets, plate, s.userPresets];
  if (matchOut && key.every((k, i) => k === matchKey[i])) return matchOut;
  const goals = deriveGoals(s);
  const base = resolvePreset(s, 'process', goals.base).config;
  const now = { ...resolvePreset(s, 'process', s.process).config, ...s.projectPresets?.process, ...plate, ...s.dirty.process };
  const keys = [...new Set([...Object.keys(base), ...Object.keys(now)])].filter(counts);
  let best: GoalMatch | null = null;
  for (const id of GOAL_IDS) {
    const g = goals.goals[id];
    const target: Config = { ...base, ...Object.fromEntries(g.deltas.map((d) => [d.key, d.to])) };
    const edits = keys.filter((k) => !same(k, now[k], target[k]));
    if (!best || edits.length < best.edits.length) best = { id, edits, goals };
  }
  matchKey = key;
  matchOut = best!;
  return best!;
}


export function goalLabel(m: GoalMatch) {
  const n = m.edits.length;
  return n ? `${GOAL_NAMES[m.id]} + ${n} edit${n === 1 ? '' : 's'}` : GOAL_NAMES[m.id];
}


export function goalPatch(s: State, id: GoalId): Pick<State, 'process' | 'dirty' | 'projectPresets'> {
  const goals = deriveGoals(s);
  const project = s.projectPresets && { ...s.projectPresets, process: Object.fromEntries(Object.entries(s.projectPresets.process).filter(([k]) => isStyleKey(k))) };
  return { process: goals.base, dirty: { ...s.dirty, process: goalEdits(s, goals.goals[id]) }, projectPresets: project };
}


export function applyGoal(id: GoalId) {
  undoable(`Goal: ${GOAL_NAMES[id]}`, () => setState((s) => goalPatch(s, id)));
}


export function goalSetKeys(s: State): Record<string, Delta & { goal: string }> {
  const m = goalMatch(s);
  const out: Record<string, Delta & { goal: string }> = {};
  for (const d of m.goals.goals[m.id].deltas) if (!m.edits.includes(d.key)) out[d.key] = { ...d, goal: GOAL_NAMES[m.id] };
  return out;
}



let last: { printer: string; id: GoalId; exact: boolean } | null = null;
subscribeStore(() => {
  const s = getState();
  if (!s.processes.length) return;
  const m = goalMatch(s);
  if (last && last.printer !== s.printer && last.exact && last.id !== 'balanced' && m.id === 'balanced' && !m.edits.length) {
    const id = last.id;
    last = { printer: s.printer, id, exact: true };
    setState((st) => goalPatch(st, id));
    return;
  }
  last = { printer: s.printer, id: m.id, exact: !m.edits.length };
});
