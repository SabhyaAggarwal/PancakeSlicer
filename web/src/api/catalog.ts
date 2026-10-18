import { normalizeConfig } from '../config-values';
import type { PresetCatalog, PresetType, SystemPreset } from './types';

export function normalizeCatalog(raw: any, vendor: string): PresetCatalog {
  const pick = (...keys: string[]) => keys.map((k) => raw?.[k]).find(Array.isArray) ?? [];
  const norm = (type: PresetType) => (p: any): SystemPreset => ({
    ...p,
    type,
    vendor: p.vendor ?? vendor,
    config: normalizeConfig(p.config ?? p.values ?? {}),
  });
  return {
    machines: pick('machines', 'machine', 'printers').map(norm('machine')),
    processes: pick('processes', 'process', 'prints').map(norm('process')),
    filaments: pick('filaments', 'filament').map(norm('filament')),
  };
}

export function demoPresets(cat: PresetCatalog, models: string[]): PresetCatalog {
  const machines = cat.machines.filter((m) => models.includes(m.model ?? String(m.config.printer_model ?? '')));
  const names = new Set(machines.map((m) => m.name));
  const forDemo = (p: { compatible_printers?: string[] }) => (p.compatible_printers ?? []).some((c) => names.has(c));
  return {
    machines,
    processes: cat.processes.filter((p) => p.name.includes('Non-planar top') && forDemo(p)),
    filaments: cat.filaments.filter(forDemo),
  };
}
