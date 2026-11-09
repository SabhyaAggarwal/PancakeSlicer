import type { Config } from '../api/types';
import { compatible, effectiveConfig, resolvePreset, type State } from '../state/store';
import { surfaceOf, surfaceUnavailable, text, type SurfaceId } from './surface';

export interface DefaultProcess {
  id: string;
  label: string;
  preset?: string;
  layer: number;
  surface: SurfaceId;
  unavailable?: string;
  note?: string;
}

const number = (v: Config[string] | undefined) => parseFloat(text(v));

export function defaultProcesses(s: State): DefaultProcess[] {
  const machine = resolvePreset(s, 'machine', s.printer);
  const list = compatible(s, 'process', machine.base?.name ?? s.printer);
  const declared = text(machine.config.default_print_profile);
  const base = list.find((p) => p.name === declared && surfaceOf(p.config) === 'flat')
    ?? list.find((p) => /standard/i.test(p.name) && surfaceOf(p.config) === 'flat')
    ?? list.find((p) => /standard/i.test(p.name)) ?? list.find((p) => p.name === declared) ?? list[0];
  const nozzle = number(effectiveConfig(s, 'machine').nozzle_diameter) || 0.4;
  const minimum = number(effectiveConfig(s, 'machine').min_layer_height) || nozzle * 0.25;
  const maximum = number(effectiveConfig(s, 'machine').max_layer_height) || nozzle * 0.75;
  return [
    { id: 'standard', name: 'Standard', target: 0.20, surface: 'flat' as const },
    { id: 'draft', name: 'Draft', target: 0.24, surface: 'flat' as const },
    { id: 'smooth-020', name: 'Smooth top', target: 0.20, surface: 'smooth' as const },
    { id: 'smooth-024', name: 'Smooth top', target: 0.24, surface: 'smooth' as const },
  ].map(({ id, name, target, surface }) => {
    const layer = Math.round(Math.max(minimum, Math.min(maximum, target)) * 1000) / 1000;
    return { id: `default:${id}`, label: `${name} ${target.toFixed(2)}`, preset: base?.name, layer, surface,
      unavailable: !base ? 'No compatible print profile found for this printer' : surfaceUnavailable(surface),
      note: layer !== target ? `Using ${layer} mm (adjusted to fit this nozzle's layer-height range)` : undefined };
  });
}
