
import { normalizeConfig } from '../config-values';
import type { CompactCatalog } from './compact-catalog';
import type { ConfigValue, PresetCatalog, PresetType } from './types';

type ServicePreset = Omit<CompactCatalog['machines'][number], 'type' | 'vendor'> & { vendor?: string };



export interface ServicePresets {
  schema: 'orca.presets.compact.v1';
  vendor: string;
  keys: string[];
  values: ConfigValue[];
  value_keys: number[];
  bases: number[][];
  machines: ServicePreset[];
  processes: ServicePreset[];
  filaments: ServicePreset[];
}


export function normalizeCatalog(raw: ServicePresets, vendor: string, native: boolean): CompactCatalog {
  if (raw?.schema !== 'orca.presets.compact.v1') throw new Error('The slicing service sent an unknown preset catalog format.');
  const values = raw.values.map((value, i) => {
    const key = raw.keys[raw.value_keys[i]];
    return normalizeConfig({ [key]: value }, native)[key];
  });
  const kind = (type: PresetType, presets: ServicePresets['machines']) => presets.map((p) => ({ ...p, type, vendor: p.vendor ?? vendor }));
  return { keys: raw.keys, values, bases: raw.bases,
    machines: kind('machine', raw.machines), processes: kind('process', raw.processes), filaments: kind('filament', raw.filaments) };
}



export function demoPresets(cat: PresetCatalog, models: string[]): PresetCatalog {
  const machines = cat.machines.filter((m) => models.includes(m.model ?? String(m.config.printer_model ?? '')));
  const names = new Set(machines.map((m) => m.name));
  const forDemo = (p: { compatible_printers?: string[] }) => (p.compatible_printers ?? []).some((c) => names.has(c));
  return {
    machines,
    processes: cat.processes.filter(forDemo),
    filaments: cat.filaments.filter((p) => forDemo(p) || !p.compatible_evaluated && !(p.compatible_printers?.length) && !p.config.compatible_printers_condition),
  };
}
