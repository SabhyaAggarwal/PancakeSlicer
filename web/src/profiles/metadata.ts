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

export interface RowField { value: string; says?: string }
const escape = (text: string) => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const mentions = (title: string, words: string) => !!words && new RegExp(`(^|[^\\w.])${escape(words)}($|[^\\w.])`, 'i').test(title);
export function rowDetail(title: string, fields: (RowField | undefined | false)[], groups: string[] = []): string {
  const said = new Set(groups.map((g) => g.toLocaleLowerCase()));
  return fields.filter((f): f is RowField => !!f && !!f.value && !said.has(f.value.toLocaleLowerCase()) && !mentions(title, f.says ?? f.value))
    .map((f) => f.value).join(' · ');
}
export const vendorField = (vendor: string): RowField => ({ value: vendor, says: vendor.split(/\s+/)[0] });
export const nozzleField = (nozzle: string): RowField | undefined => nozzle ? { value: `${nozzle} mm`, says: nozzle } : undefined;

export function vendorLabel(config: Config, vendor: string): string {
  return (first(config.printer_vendor) || vendor || 'Custom').replace(/^BBL$/, 'Bambu Lab');
}
export function modelLabel(config: Config, vendor: string, model?: string): string {
  const full = first(config.printer_model) || model || '', brand = vendorLabel(config, vendor);
  return full.startsWith(`${brand} `) ? full.slice(brand.length + 1) : full;
}
export function printerLine(config: Config, vendor: string, model?: string): string {
  const nozzle = first(config.nozzle_diameter);
  return [modelLabel(config, vendor, model), nozzle && `${nozzle} mm`].filter(Boolean).join(' · ');
}
export function processMode(config: Config): string {
  return profileSummary('process', config).split(' · ').pop() ?? '';
}
export function processTitle(name: string, config: Config, scoped: boolean): string {
  let title = name;
  const lead = /^(\d+(?:\.\d+)?)\s*mm\s+/i.exec(title);
  if (lead && Number(lead[1]) === Number(first(config.layer_height))) title = title.slice(lead[0].length);
  if (scoped) title = title.replace(/\s*@.*$/, '');
  return title.trim() || name;
}

export function profileColour(config:Config):string|null {
  const value=first(config.filament_colour ?? config.default_filament_colour);
  return /^#[\da-f]{6}$/i.test(value) ? value.toUpperCase() : null;
}
export function printerCover(vendor:string,config:Config,model?:string):string|undefined {
  const actual=first(config.printer_model) || model;
  return vendor && actual ? `/profiles/${encodeURIComponent(vendor)}/${encodeURIComponent(actual)}_cover.png` : undefined;
}
