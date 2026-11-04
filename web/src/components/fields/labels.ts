export const SHORT_LABELS: Record<string, string> = {
  nonplanar_deform: 'Curved whole part',
  nonplanar_adaptive_bands: 'Adaptive shell height',
  nonplanar_surface_resolution: 'Surface resolution',
  nonplanar_surface_spacing: 'Line spacing',
  elefant_foot_compensation: 'Elephant foot',
  hole_to_polyhole: 'Holes to polyholes',
  only_one_wall_top: 'One wall on top',
  only_one_wall_first_layer: 'One wall, 1st layer',
  nonplanar_surface_flow_mod: 'Flow modulation',
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
