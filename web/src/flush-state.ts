import type { Config } from './api/types';
import { calcFlushingVolumes } from './flush';
import { OPTIONS } from './options';
import { filamentColor } from './plater/filamentColor';
import { effectiveConfig, type State } from './state/store';

export function flushInputs(s: State) {
  const colours = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
  const printer = effectiveConfig(s, 'machine');
  const filaments: Config[] = s.filamentSlots.map((_, i) => effectiveConfig(s, 'filament', i));
  return { colours, printer, filaments };
}

export function autoFlushMatrix(s: State): number[] {
  const { colours, printer, filaments } = flushInputs(s);
  return calcFlushingVolumes(colours, printer, filaments);
}

export function defaultMultiplier(s: State): number {
  const v = effectiveConfig(s, 'process').flush_multiplier ?? (OPTIONS.flush_multiplier?.default as number[] | undefined);
  const first = Array.isArray(v) ? v[0] : v;
  return Number.isFinite(Number(first)) ? Number(first) : 1;
}

export function currentFlush(s: State): { matrix: number[]; multiplier: number } {
  const n = s.filamentSlots.length;
  const table = s.flush.matrix ?? (s.project ? s.projectMaterials?.matrix : null);
  const saved = table?.length === n * n ? table : null;
  return { matrix: saved ?? autoFlushMatrix(s), multiplier: s.flush.multiplier ?? (s.project ? s.projectMaterials?.multiplier : null) ?? defaultMultiplier(s) };
}

export function flushModified(s: State): boolean {
  if (s.filamentSlots.length < 2) return false;
  const { matrix, multiplier } = currentFlush(s);
  const auto = autoFlushMatrix(s);
  return multiplier !== defaultMultiplier(s) || matrix.some((v, i) => v !== auto[i]);
}
