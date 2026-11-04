import type { Config } from '../api/types';
import { effectiveConfig, getState, markStale, setState, type State } from '../state/store';
import { undoable } from '../state/history';
import { listedPlates } from '../plater/plates';
import { extruderCount, valueAt, valuesOf } from './config';

export const NOZZLE_MAP_KEYS = ['filament_map_mode', 'filament_map', 'filament_volume_map', 'filament_nozzle_map'] as const;
export const VOLUME_TYPES: Record<string, number> = { Standard: 0, 'High Flow': 1, Hybrid: 2, 'TPU High Flow': 3, 'E3D High Flow': 5, 'Extra High Flow': 6 };

export function nozzleAssignments(s: State): Config {
  const machine = effectiveConfig(s, 'machine');
  const process = effectiveConfig(s, 'process');
  const plate = s.projectPlates.find((p) => p.index === s.plateIndex)?.settings ?? {};
  const imported = s.projectPresets?.machine ?? {};
  return Object.fromEntries(NOZZLE_MAP_KEYS.flatMap((key) => {
    const value = plate[key] ?? imported[key] ?? process[key] ?? machine[key];
    return value === undefined ? [] : [[key, value]];
  }));
}

export function assignedExtruder(s: State, slot: number): number {
  return Number(valueAt(nozzleAssignments(s).filament_map, slot, String(effectiveConfig(s, 'machine').master_extruder_id ?? 1))) - 1;
}

export function setNozzleAssignment(slot: number, extruder: number, volume?: number) {
  const s = getState(), machine = effectiveConfig(s, 'machine');
  if (slot < 0 || slot >= s.filamentSlots.length || extruder < 0 || extruder >= extruderCount(machine)) return;
  const previous = nozzleAssignments(s);
  const map = Array.from({ length: s.filamentSlots.length }, (_, i) => String(i === slot ? extruder + 1 : assignedExtruder(s, i) + 1));
  const volumes = map.map((id, i) => i === slot && volume !== undefined ? String(volume) : valueAt(previous.filament_volume_map, i,
    String(VOLUME_TYPES[valueAt(machine.nozzle_volume_type ?? machine.default_nozzle_volume_type, Number(id) - 1, 'Standard')] ?? 0)));
  updateAssignments({ filament_map_mode: 'Manual', filament_map: map, filament_volume_map: volumes });
}

export function updateAssignments(config: Config) {
  undoable('Nozzle assignment', () => setState((s) => ({
    projectPlates: listedPlates(s).map((p) => p.index === s.plateIndex ? { ...p, settings: { ...p.settings, ...config } } : p),
    job: markStale(s.job),
  })));
}

export function remapNozzleAssignments(config: Config, map: number[], kept?: number[]): Config {
  const out = { ...config };
  for (const key of ['filament_map', 'filament_volume_map', 'filament_nozzle_map']) {
    if (config[key] === undefined) continue;
    const values = valuesOf(config[key]);
    if (kept) out[key] = kept.map((i) => valueAt(values, i, key === 'filament_volume_map' ? '0' : '1'));
    else {
      const next: string[] = Array.from({ length: Math.max(0, ...map) }, () => key === 'filament_volume_map' ? '0' : '1');
      map.forEach((slot, old) => { if (slot > 0) next[slot - 1] = valueAt(values, old, next[slot - 1]); });
      out[key] = next;
    }
  }
  return out;
}
