import type { Config, PresetType } from '../api/types';
import { resolveProfile, type ResolvedProfile } from './model';
import { effectiveProfileValues, type ProfileContextState } from './context';
import { coversPrinter, type DynamicRecord } from './dynamic';
import { matchesFilament, matchesPrint, matchesPrinter, validWorksWith, type FilamentFacts, type PrinterFacts, type WorksWithRule } from './works-with';
export type ProfileApplicability = WorksWithRule;

const first = (value: Config[string] | undefined) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '').split(/[;,]/)[0];
export function printerFacts(s: ProfileContextState, key: string, nozzle?: string | number): PrinterFacts {
  const resolved = resolveProfile(s, 'machine', key), config = key === s.printer ? effectiveProfileValues(s, 'machine', key) : resolved.config;
  return { id: resolved.key, name: resolved.name, printerModel: first(config.printer_model), nozzle: Number(nozzle ?? first(config.nozzle_diameter)) };
}
const filamentFacts = (resolved: ResolvedProfile): FilamentFacts => ({ id: resolved.key, type: first(resolved.config.filament_type) });
const inherit1 = (v: unknown) => !!v && (v as { schema?: unknown; mode?: unknown }).schema === 1 && (v as { mode?: unknown }).mode === 'inherit';
const ruleOf = (resolved: ResolvedProfile) => (inherit1(resolved.source?.applicability) ? undefined : resolved.source?.applicability as WorksWithRule | undefined);

export function worksWithReason(s: ProfileContextState, type: PresetType, key: string, printerKey = s.printer, nozzle?: string | number): string {
  const resolved = resolveProfile(s, type, key), rule = ruleOf(resolved);
  if (rule !== undefined && !validWorksWith(rule, type)) return 'Repair this profile\'s Works with rule';
  if (type === 'machine') return '';
  const printer = printerFacts(s, printerKey, nozzle);
  if (rule && !matchesPrinter(rule, printer)) return 'Not set to work with this printer or nozzle';
  const dynamic = resolved.source as DynamicRecord | undefined;
  if (dynamic?.dynamic && !coversPrinter(dynamic, { printerModel: printer.printerModel, nozzle: printer.nozzle, printerId: printer.id }))
    return type === 'process' ? 'No layer height for this printer and nozzle' : 'Not available for this printer and nozzle';
  const process = resolveProfile(s, 'process', s.process), loaded = (s.filamentSlots ?? []).filter(Boolean).map((k) => resolveProfile(s, 'filament', k));
  if (type === 'filament') {
    if (rule && !matchesPrint(rule, process.key)) return 'Not set to work with these print settings';
    for (const other of [process, resolveProfile(s, 'machine', printerKey)]) {
      const otherRule = ruleOf(other);
      if (otherRule && validWorksWith(otherRule, other.source?.type ?? 'process') && !matchesFilament(otherRule, filamentFacts(resolved)))
        return `${other.name ?? 'The selection'} isn't set to work with this filament`;
    }
    return '';
  }
  for (const filament of loaded) {
    if (rule && !matchesFilament(rule, filamentFacts(filament))) return `Not set to work with ${filament.name ?? 'this filament'}`;
    const filamentRule = ruleOf(filament);
    if (filamentRule && validWorksWith(filamentRule, 'filament') && !matchesPrint(filamentRule, resolved.key))
      return `${filament.name ?? 'A loaded filament'} isn't set to work with these print settings`;
  }
  return '';
}
