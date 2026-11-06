import type { Config } from '../api/types';
import { filamentVectors } from '../multinozzle/config';
import { effectiveProfileValues } from '../profiles/context';
import { resolveProfile } from '../profiles/model';
import { getState } from '../state/store';

export interface DemoResolveInput { printer: string; print: string; filaments: string[]; layerHeight?: number; project?: Config }

export async function resolveDemo(input: DemoResolveInput) {
  if (!input.printer || !input.print || !input.filaments?.length) throw new Error('Choose a printer, print profile and filament.');
  const context = { ...getState(), printer: input.printer, process: input.print, filamentSlots: input.filaments,
    processVariant: input.layerHeight ? { layerHeight: input.layerHeight } : null, dirty: { machine: {}, process: {} },
    filamentEdits: {}, projectPresets: null, profileSnapshots: null, projectPlates: [] };
  const machine = effectiveProfileValues(context, 'machine', input.printer), print = effectiveProfileValues(context, 'process', input.print);
  const filaments = input.filaments.map((key, slot) => effectiveProfileValues(context, 'filament', key, slot));
  const config = { ...machine, ...print, ...filamentVectors(filaments, {}), ...input.project };
  if (config.nozzle_volume_type === undefined && machine.default_nozzle_volume_type !== undefined) config.nozzle_volume_type = machine.default_nozzle_volume_type;
  const sources: Record<string, string> = Object.fromEntries(Object.keys(config).map(key => [key, 'catalog']));
  const printer = resolveProfile(context, 'machine', input.printer), process = resolveProfile(context, 'process', input.print);
  const mark = (values: Config | undefined, source: string) => { for (const key of Object.keys(values ?? {})) sources[key] = source; };
  mark(printer.user?.overlay, 'printer'); mark(process.user?.overlay, 'print');
  for (const row of (process.user as { auto?: { scales?: { key: string }[] } } | undefined)?.auto?.scales ?? []) sources[row.key] = 'scale';
  sources.layer_height = 'print';
  mark((printer.user as { printOverrides?: Config } | undefined)?.printOverrides, 'printer override');
  for (const key of input.filaments) mark(resolveProfile(context, 'filament', key).user?.overlay, 'filament');
  mark(input.project, 'project');
  return { config, sources, engine: { vendor: printer.base?.vendor, printer: printer.base?.name,
    process: (printer.base as { standard_process?: string } | undefined)?.standard_process,
    filaments: input.filaments.map(key => resolveProfile(context, 'filament', key).base?.name) } };
}
