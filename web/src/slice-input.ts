import type { Config, ConfigValue } from './api/types';
import { currentFlush } from './flush-state';
import { eventArg } from './components/LayerEvents';
import { OPTIONS, jobValue } from './options';
import { filamentColor } from './plater/filamentColor';
import { meshSignature, plateDescription } from './plater/geometry';
import { projectInstances } from './plater/projectTransforms';
import { effectiveConfig, getState, printerPreset, resolvePreset, type State } from './state/store';

export function outputKind(s = getState()): 'gcode' | 'gcode.3mf' {
  const m = printerPreset(s);
  return m?.vendor === 'BBL' ? 'gcode.3mf' : 'gcode';
}

export function overrides(s = getState()): Config {
  const out: Config = s.project ? {
    ...effectiveConfig(s, 'machine'), ...effectiveConfig(s, 'process'),
    ...effectiveConfig(s, 'filament'),
  } : {};
  const add = (type: 'machine' | 'process' | 'filament', name: string, dirty: Config) => {
    Object.assign(out, resolvePreset(s, type, name).user?.overrides ?? {}, dirty);
  };
  add('machine', s.printer, s.dirty.machine);
  add('process', s.process, s.dirty.process);
  const configs = s.filamentSlots.map((_, i) => effectiveConfig(s, 'filament', i));
  for (const k of new Set(configs.flatMap((c) => Object.keys(c)))) {
    if (!OPTIONS[k]) continue;
    const values = configs.map((c) => c[k] ?? OPTIONS[k].default);
    if (values.every((v) => v === undefined || Array.isArray(v) && !v.length)) continue;
    out[k] = values.some(Array.isArray)
      ? values.map((v) => Array.isArray(v) ? v[0] ?? '' : v ?? '') as ConfigValue
      : values[0] as ConfigValue;
  }
  for (const k of Object.keys(out)) if (!OPTIONS[k]) delete out[k];
  if (s.projectMaterials && s.project) {
    out.filament_colour = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
    if (s.projectMaterials.matrix) out.flush_volumes_matrix = s.projectMaterials.matrix;
    if (s.projectMaterials.multiplier !== null) out.flush_multiplier = [s.projectMaterials.multiplier];
  }
  if (s.bedType) out.curr_bed_type = s.bedType;
  const plateBed = s.projectPlates.find((p) => p.index === s.plateIndex)?.settings.curr_bed_type;
  if (plateBed && !s.dirty.machine.curr_bed_type) out.curr_bed_type = plateBed;
  if (s.filamentSlots.length > 1 || s.slotColours.some((c) => c !== undefined)) {
    const { matrix, multiplier } = currentFlush(s);
    if (!s.project || s.slotColours.some((c) => c !== undefined))
      out.filament_colour = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
    if (!s.project || s.flush.matrix !== null) out.flush_volumes_matrix = matrix;
    if (!s.project || s.flush.multiplier !== null) out.flush_multiplier = [multiplier];
  }
  if (s.projectPlates.length > 1) for (const key of ['wipe_tower_x', 'wipe_tower_y']) {
    const value = out[key];
    if (Array.isArray(value) && value.length) out[key] = [Number(value[Math.min(s.plateIndex - 1, value.length - 1)])];
  }
  for (const k of Object.keys(out)) out[k] = jobValue(k, out[k]);
  return out;
}

export function jobFields(s: State, saving = false) {
  const sys = (type: 'machine' | 'process' | 'filament', n: string) => resolvePreset(s, type, n).base?.name ?? n;
  return {
    vendor: printerPreset(s)?.vendor ?? 'BBL',
    printer: sys('machine', s.printer),
    process: sys('process', s.process),
    filaments: s.filamentSlots.map((f) => sys('filament', f)),
    ...(s.projectPlates.length > 1 ? { plate_index: s.plateIndex } : {}),
    ...(s.project ? { project_instances: projectInstances(s.projectPlates.length ? s.projectPlates.flatMap((p) => p.index === s.plateIndex ? s.objects : p.objects).sort((a,b) => (a.project?.buildIndex ?? 0) - (b.project?.buildIndex ?? 0)) : s.objects) } : {}),
    overrides: overrides(saving ? { ...s, projectPlates: [] } : s),
    events: s.events.map(eventArg),
    output: outputKind(s),
  };
}

export function plateSignature(s: State): string {
  return JSON.stringify({
    ...jobFields(s),
    plate: s.project ? undefined : plateDescription(s.objects),
    files: s.project ? [fileId(s.project)] : s.objects.map(meshSignature),
  }, (key, value) => key === 'transform' && Array.isArray(value) ? value.map((v: number) => Math.round(v * 1e6) / 1e6)
    : value && typeof value === 'object' && !Array.isArray(value)
    ? Object.fromEntries(Object.entries(value).sort(([a], [b]) => a.localeCompare(b))) : value);
}

const fileId = (file: File) => `${file.name}:${file.size}:${file.lastModified}`;
