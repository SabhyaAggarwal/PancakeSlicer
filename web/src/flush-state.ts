
import type { Config } from './api/types';
import { OPTIONS } from './options';
import { filamentColor } from './plater/filamentColor';
import { effectiveConfig, setState, type State } from './state/store';
import { extruderCount, installedVariant, valueAt } from './multinozzle/config';
import { filament_options_with_variant, printer_options_with_variant_1 } from './multinozzle/variant-keys';



let flush: typeof import('./flush') | null = null;
export const flushReady = () => (flush ? Promise.resolve() : import('./flush').then((m) => { if (!flush) { flush = m; setState({}); } }));
setTimeout(() => void flushReady());

export function flushInputs(s: State, nozzle = 0) {
  const colours = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
  const machine = effectiveConfig(s, 'machine');
  const variant = installedVariant(machine, 'machine', machine, nozzle);
  const printer = { ...machine, ...Object.fromEntries([...printer_options_with_variant_1].filter((k) => machine[k] !== undefined).map((k) => [k, [valueAt(machine[k], variant)]])) };
  const filaments: Config[] = s.filamentSlots.map((_, i) => {
    const config = effectiveConfig(s, 'filament', i), variant = installedVariant(config, 'filament', machine, nozzle);
    return { ...config, ...Object.fromEntries([...filament_options_with_variant].filter((k) => config[k] !== undefined).map((k) => [k, [valueAt(config[k], variant)]])) };
  });
  return { colours, printer, filaments };
}

export function autoFlushMatrix(s: State, nozzle = 0): number[] {
  const { colours, printer, filaments } = flushInputs(s, nozzle);
  return flush ? flush.calcFlushingVolumes(colours, printer, filaments) : [];
}

export function defaultMultiplier(s: State, nozzle = 0): number {
  const v = effectiveConfig(s, 'process').flush_multiplier ?? (OPTIONS.flush_multiplier?.default as number[] | undefined);
  const first = Array.isArray(v) ? v[Math.min(nozzle, v.length - 1)] : v;
  return Number.isFinite(Number(first)) ? Number(first) : 1;
}


export function currentFlush(s: State, nozzle = 0): { matrix: number[]; multiplier: number } {
  const n = s.filamentSlots.length;
  const table = s.flush.matrix ?? (s.project ? s.projectMaterials?.matrix : null);
  const count = extruderCount(effectiveConfig(s, 'machine'));
  const saved = table?.length === n * n * count ? table.slice(nozzle * n * n, (nozzle + 1) * n * n) : table?.length === n * n ? table : null;
  return { matrix: saved ?? autoFlushMatrix(s, nozzle), multiplier: s.flush.multiplier ?? defaultMultiplier(s, nozzle) };
}


export function allFlush(s: State): { matrix: number[]; multipliers: number[] } {
  const tables = Array.from({ length: extruderCount(effectiveConfig(s, 'machine')) }, (_, nozzle) => currentFlush(s, nozzle));
  return { matrix: tables.flatMap((t) => t.matrix), multipliers: tables.map((t) => t.multiplier) };
}


export function flushModified(s: State): boolean {
  if (s.filamentSlots.length < 2 || !flush) return false;
  const { flush_multiplier: _multiplier, ...process } = s.dirty.process;
  const unedited = { ...s, dirty: { ...s.dirty, process } };
  return Array.from({ length: extruderCount(effectiveConfig(s, 'machine')) }, (_, nozzle) => {
    const { matrix, multiplier } = currentFlush(s, nozzle), auto = autoFlushMatrix(s, nozzle);
    return multiplier !== defaultMultiplier(unedited, nozzle) || matrix.some((v, i) => v !== auto[i]);
  }).some(Boolean);
}
