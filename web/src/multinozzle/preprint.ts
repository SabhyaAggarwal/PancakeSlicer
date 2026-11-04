import type { Config } from '../api/types';
import type { PrinterStatus, Tray } from '../printers/connections';
import { extruderCount, extruderLabel, installedVariant, valueAt } from './config';
import type { SendFilament } from './ams';

export function preprintWarnings(machine: Config, status: PrinterStatus | undefined, filaments: SendFilament[], mapping: number[], trays: Tray[]): string[] {
  const warnings: string[] = [], count = extruderCount(machine);
  if (status?.online && status.nozzles?.length) for (let extruder = 0; extruder < count; extruder++) {
    if (filaments.length && filaments.every((f) => f.extruder !== undefined && f.extruder !== extruder)) continue;
    const actual = status.nozzles.filter((n) => n.extruder === extruder && (count === 1 || !n.legacy));
    if (!actual.length) continue;
    const diameter = Number(valueAt(machine.nozzle_diameter, extruder));
    const flow = valueAt(machine.nozzle_volume_type ?? machine.default_nozzle_volume_type, extruder, 'Standard');
    const variant = installedVariant(machine, 'machine', machine, extruder);
    const material = valueAt(machine.nozzle_type, variant);
    const matches = actual.filter((n) => !n.abnormal && Math.abs(n.diameter - diameter) < 0.001 && (!n.flow || flow === 'Hybrid' || n.flow === flow));
    const label = extruderLabel(extruder, count, true);
    if (!matches.length) warnings.push(`${label}: the printer has no available ${diameter} mm ${flow} nozzle matching this slice.`);
    else if (material && material !== 'undefine' && matches.every((n) => n.type && n.type !== material))
      warnings.push(`${label}: the reported nozzle material differs from the ${material.replaceAll('_', ' ')} selected for this slice.`);
  }
  filaments.forEach((f, i) => {
    const tray = trays.find((t) => t.id === mapping[i]);
    if (tray?.type && tray.type.toUpperCase() !== f.type.toUpperCase()) warnings.push(`Filament ${i + 1}: sliced for ${f.type}, mapped to ${tray.type}.`);
  });
  return warnings;
}
