import type { Config, PresetType } from '../api/types';
import { resolveProfile, type ResolvedProfile } from './model';
import { effectiveProfileValues, profileChain, type ProfileContextState } from './context';
import { chainApplicability } from './chain';
import { matchesFilament, matchesPrint, matchesPrinter, validWorksWith, type FilamentFacts, type PrinterFacts, type WorksWithRule } from './works-with';
export type ProfileApplicability = WorksWithRule;

const first = (value: Config[string] | undefined) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '').split(/[;,]/)[0];
export function printerFacts(s: ProfileContextState, key: string, nozzle?: string | number): PrinterFacts {
  const resolved = resolveProfile(s, 'machine', key), config = key === s.printer ? effectiveProfileValues(s, 'machine', key) : resolved.config;
  return { id: resolved.key, name: resolved.name, printerModel: first(config.printer_model), nozzle: Number(nozzle ?? first(config.nozzle_diameter)) };
}
const filamentFacts = (resolved: ResolvedProfile): FilamentFacts => ({ id: resolved.key, type: first(resolved.config.filament_type) });
const ruleOf = (s: ProfileContextState, type: PresetType, resolved: ResolvedProfile) => chainApplicability(profileChain(s, type, resolved)) as WorksWithRule | undefined;
export const hasWorksWithRule = (s: ProfileContextState, type: PresetType, key: string) => ruleOf(s, type, resolveProfile(s, type, key)) !== undefined;

export function worksWithReason(s: ProfileContextState, type: PresetType, key: string, printerKey = s.printer, nozzle?: string | number): string {
  const resolved = resolveProfile(s, type, key), rule = ruleOf(s, type, resolved);
  if (rule !== undefined && !validWorksWith(rule, type)) return 'Fix this profile\'s compatibility rules';
  if (type === 'machine') return '';
  const printer = printerFacts(s, printerKey, nozzle);
  if (rule && !matchesPrinter(rule, printer)) return 'Not marked compatible with this printer or nozzle';
  const process = resolveProfile(s, 'process', s.process), loaded = (s.filamentSlots ?? []).filter(Boolean).map((k) => resolveProfile(s, 'filament', k));
  if (type === 'filament') {
    if (rule && !matchesPrint(rule, process.key)) return 'Not marked compatible with these print settings';
    for (const other of [process, resolveProfile(s, 'machine', printerKey)]) {
      const otherRule = ruleOf(s, other === process ? 'process' : 'machine', other);
      if (otherRule && validWorksWith(otherRule, other === process ? 'process' : 'machine') && !matchesFilament(otherRule, filamentFacts(resolved)))
        return `${other.name ?? 'The selection'} isn't set to work with this filament`;
    }
    return '';
  }
  for (const filament of loaded) {
    if (rule && !matchesFilament(rule, filamentFacts(filament))) return `Not marked compatible with ${filament.name ?? 'this filament'}`;
    const filamentRule = ruleOf(s, 'filament', filament);
    if (filamentRule && validWorksWith(filamentRule, 'filament') && !matchesPrint(filamentRule, resolved.key))
      return `${filament.name ?? 'A loaded filament'} isn't set to work with these print settings`;
  }
  return '';
}
