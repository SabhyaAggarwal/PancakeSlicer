import type { Config, PresetType } from '../api/types';
const first = (v: unknown) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');
export const PROFILE_ROLE: Record<PresetType, string> = {machine: 'Printer', filament: 'Filament', process: 'Print settings'};
export function profileSummary(type: PresetType, config: Config, model?: string): string {
  if (type === 'machine') return [model || first(config.printer_model), first(config.nozzle_diameter) && `${first(config.nozzle_diameter)} mm nozzle`].filter(Boolean).join(' · ');
  if (type === 'filament') return [first(config.filament_type), first(config.filament_vendor) !== 'Generic' ? first(config.filament_vendor) : ''].filter(Boolean).join(' · ');
  const height = Number(first(config.layer_height));
  const mode = first(config.nonplanar_mode), wave = first(config.nonplanar_wave);
  const top = wave && wave !== 'off' ? 'Waves' : mode === 'curved_tops' ? 'Curved top' : mode === 'off' || !mode ? 'Flat' : mode === 'auto' ? 'Automatic' : 'Custom surface';
  return [height > 0 ? `${height.toFixed(2)} mm` : '', top].filter(Boolean).join(' · ');
}

export function profileColour(config:Config):string|null {
  const value=first(config.filament_colour ?? config.default_filament_colour);
  return /^#[\da-f]{6}$/i.test(value) ? value.toUpperCase() : null;
}
export function printerCover(vendor:string,config:Config,model?:string):string|undefined {
  const actual=first(config.printer_model) || model;
  return vendor && actual ? `/profiles/${encodeURIComponent(vendor)}/${encodeURIComponent(actual)}_cover.png` : undefined;
}
