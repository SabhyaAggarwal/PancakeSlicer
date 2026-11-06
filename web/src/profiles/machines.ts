import type { SystemPreset } from '../api/types';
import { resolvePreset, type State } from '../state/store';

export const nozzleOf = (preset: SystemPreset) => Number([preset.config.nozzle_diameter].flat()[0]);
export const modelKey = (preset: SystemPreset) => JSON.stringify([preset.vendor, preset.model || preset.name]);


export function printerModels(s: State) {
  const models = new Map<string, SystemPreset[]>();
  const saved = s.userPresets.filter((p) => p.type === 'machine').flatMap((p) => {
    const { base, config } = resolvePreset(s, 'machine', p.name);
    return base ? [{ ...base, model: base.model || base.name, name: p.name, config }] : [];
  });
  for (const preset of [...s.machines, ...saved]) {
    const key = modelKey(preset);
    const list = models.get(key) ?? [];
    list.push(preset);
    models.set(key, list);
  }
  return models;
}


export function modelPreset(list: SystemPreset[], printer: string, diameter: number) {
  return list.find((p) => p.name === printer) ?? list.find((p) => nozzleOf(p) === diameter)
    ?? list.find((p) => nozzleOf(p) === 0.4) ?? list[0];
}









export function filamentSlots(printer?: SystemPreset): number {
  if (!printer) return 1;
  const c = printer.config;
  const text = (k: string) => [c[k]].flat().join('\n');
  const extruders = Math.max(1, [c.nozzle_diameter].flat().length);
  const ams = /\bM620\b/.test(text('change_filament_gcode')) ? 4 : 1;
  const sem = [c.single_extruder_multi_material].flat()[0];
  const mmu = (sem === 1 || sem === '1' || sem === true) && /MMU|\bM702\b/i.test(`${printer.model ?? ''} ${printer.name} ${text('machine_start_gcode')} ${text('change_filament_gcode')}`) ? 5 : 1;
  return Math.max(extruders, ams, mmu);
}
