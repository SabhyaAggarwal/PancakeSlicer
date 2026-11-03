import { normalizeConfig } from '../config-values';
import type { PresetCatalog, PresetType, SystemPreset } from './types';

export function normalizeCatalog(raw: any, vendor: string, native = raw?.schema === 'orca.presets.v1'): PresetCatalog {
  const pick = (...keys: string[]) => keys.map((k) => raw?.[k]).find(Array.isArray) ?? [];
  const norm = (type: PresetType) => (p: any): SystemPreset => ({
    ...p,
    type,
    vendor: p.vendor ?? vendor,
    config: p.config ?? p.values ?? {},
  });
  const catalog = {
    machines: pick('machines', 'machine', 'printers').map(norm('machine')),
    processes: pick('processes', 'process', 'prints').map(norm('process')),
    filaments: pick('filaments', 'filament').map(norm('filament')),
  };
  for (const presets of Object.values(catalog))
    for (const preset of presets) preset.config = normalizeConfig(preset.config, native);
  return catalog;
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
