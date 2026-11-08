import { resolvePreset, type State } from '../state/store';

export function printerModel(state: State, printer: string): string {
  const { config, base } = resolvePreset(state, 'machine', printer);
  const model = config.printer_model;
  return String((Array.isArray(model) ? model[0] : model) || base?.model || '');
}

export function printerCapabilities(model: string) {
  const openBed = /\bA1(?:\s+mini)?\b/i.test(model);
  return { chamber: !openBed, timelapse: !/\bP1P\b/i.test(model), fans: openBed ? ['part'] : ['part', 'auxiliary', 'chamber'],
    lightLabel: openBed ? 'Light' : 'Chamber light', cameraLabel: openBed ? 'Camera' : undefined };
}
