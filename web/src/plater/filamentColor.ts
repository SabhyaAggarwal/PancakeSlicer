import { effectiveConfig, type State } from '../state/store';

export function filamentColor(s: State, extruder: number): string {
  const chosen = s.slotColours[extruder - 1] ?? (s.project ? s.projectMaterials?.colours[extruder - 1] : undefined);
  if (chosen) return chosen;
  const name = s.filamentSlots[extruder - 1] ?? s.filamentSlots[0];
  const cfg = name ? effectiveConfig(s, 'filament', extruder - 1) : {};
  const c = cfg.filament_colour ?? cfg.default_filament_colour;
  const v = Array.isArray(c) ? String(c[0]) : String(c ?? '');
  return /^#[0-9a-fA-F]{6}/.test(v) ? v.slice(0, 7) : '#00AE42';
}

