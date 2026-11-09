export const SHORT_LABELS: Record<string, string> = {
  nonplanar_deform: 'Curved whole part',
  nonplanar_adaptive_bands: 'Adaptive shell height',
  nonplanar_surface_resolution: 'Surface resolution',
  nonplanar_surface_spacing: 'Line spacing',
  elefant_foot_compensation: 'Elephant foot',
  hole_to_polyhole: 'Holes to polyholes',
  only_one_wall_top: 'One wall on top',
  only_one_wall_first_layer: 'One wall, 1st layer',
  nonplanar_flow_mod: 'Flow modulation',
  nonplanar_wave: 'Waves', nonplanar_wave_wavelength: 'Wavelength',
  zaa_enabled: 'Z contouring', zaa_minimize_perimeter_height: 'Wall height angle', zaa_dont_alternate_fill_direction: 'Keep fill direction',
  nonplanar_sublayer_walls: 'Half-height walls',
  nonplanar_ignore_duct: 'Ignore cooling duct', nonplanar_curved_max_angle: 'Max surface angle', nonplanar_collision_check: 'Clearance check',
  nonplanar_repair_iterations: 'Repair passes',
  nonplanar_curved_top_layers: 'Top layers', nonplanar_top_direction: 'Top direction', nonplanar_top_finish_flow: 'Top flow', nonplanar_top_order: 'Line order',
  nonplanar_top_speed_factor: 'Top speed', nonplanar_top_fan_boost: 'Top fan boost', nonplanar_road_cooling: 'Line cooling',
  nonplanar_curved_shell_flow: 'Shell flow', nonplanar_curved_shell_speed: 'Shell speed', nonplanar_curved_min_area: 'Min top area',
  nonplanar_max_patch_height: 'Max band height', nonplanar_conformal_ramp_layers: 'Ramp layers', nonplanar_conformal_rounding: 'Shell rounding',
  nonplanar_conformal_fallback_angle: 'Fallback angle',
  nonplanar_slant_angle: 'Tilt', nonplanar_slant_direction: 'Direction', nonplanar_slant_start_height: 'Start height', nonplanar_slant_morph_layers: 'Morph layers',
};

const SWAPS: [RegExp, string][] = [
  [/^Recommended nozzle temperature$/, 'Nozzle temp. range'], [/^(Enable|Use|Activate) /, ''], [/ \(beta\)$/, ''], [/\btemperature\b/, 'temp'], [/\binitial layer\b/, '1st layer'], [/\bfirst layer\b/, '1st layer'],
  [/\bMaximum\b/, 'Max'], [/\bMinimum\b/, 'Min'], [/\bmaximum\b/, 'max'], [/\bminimum\b/, 'min'], [/\bthreshold\b/, 'limit'], [/\bcompensation\b/, 'comp.'],
  [/\b[Aa]cceleration\b/, 'accel.'], [/\bperimeters?\b/, 'wall'], [/\bdiameter\b/, 'dia.'], [/\bexternal\b/, 'ext.'], [/\binternal\b/, 'int.'], [/\bfilament\b/, 'filament'],
];

export function displayLabel(key: string, label: string): string {
  if (SHORT_LABELS[key]) return SHORT_LABELS[key];
  if (label.length <= 20) return label;
  const out = SWAPS.reduce((l, [re, to]) => l.replace(re, to), label);
  return out.charAt(0).toUpperCase() + out.slice(1);
}
