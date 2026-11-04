import { filamentVectors } from './multinozzle/config';
import { nozzleAssignments } from './multinozzle/assignments';
import type { Config, ConfigValue, JobLibrary } from './api/types';
import { allFlush } from './flush-state';
import { OPTIONS, jobValue } from './options';
import { filamentColor } from './plater/filamentColor';
import { meshSignature, plateDescription } from './plater/geometry';
import { projectEdits, projectInstances } from './plater/projectTransforms';
import { canEditProject } from './api/client';
import { effectiveLayerHeight } from './profiles/context';
import { canonicalModeConfig } from './nonplanar-mode';
import { effectiveConfig, getState, presetEdits, printerPreset, resolvePreset, type LayerEvent, type Mix, type State } from './state/store';

function eventArg(e: LayerEvent): string {
  const z = String(Math.round(e.z * 1e4) / 1e4);
  if (e.type === 'custom') return `${z}:custom:${(e.text ?? '').replace(/\r?\n/g, '\\n')}`;
  if (e.type === 'change') return `${z}:change:${e.text}`;
  return `${z}:${e.type}`;
}

export function outputKind(s = getState()): 'gcode' | 'gcode.3mf' {
  const m = printerPreset(s);
  return m?.vendor === 'BBL' ? 'gcode.3mf' : 'gcode';
}

export const MIX_KEYS = ['filament_is_mixed', 'filament_mixed_components', 'filament_mixed_sublayer_ratios', 'filament_mixed_gradient',
  'filament_mixed_gradient_range', 'filament_mixed_gradient_curve', 'filament_mixed_gradient_per_part'];

export function overrides(s = getState()): Config {
  const out: Config = { ...s.projectPresets?.machine, ...s.dirty.machine, ...s.projectPresets?.process, ...s.dirty.process };
  const slots = s.filamentSlots.map((key, i) => ({ ...s.projectPresets?.filaments[i], ...s.filamentEdits[key] }));
  const edited = new Set(slots.flatMap((c) => Object.keys(c)));
  if (edited.size) {
    const configs = s.filamentSlots.map((_, i) => effectiveConfig(s, 'filament', i));
    const assembled = filamentVectors(configs, Object.fromEntries([...edited].flatMap((key) => OPTIONS[key]?.default === undefined ? [] : [[key, OPTIONS[key].default as ConfigValue]])));
    Object.assign(out, Object.fromEntries(Object.entries(assembled).filter(([key]) => edited.has(key) || key === 'filament_self_index' || key === 'filament_extruder_variant')));
  }
  delete out.layer_height;
  Object.assign(out, nozzleAssignments(s));
  for (const k of Object.keys(out)) if (!OPTIONS[k]) delete out[k];
  if (s.projectMaterials && s.project) {
    out.filament_colour = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
    if (s.projectMaterials.matrix) out.flush_volumes_matrix = s.projectMaterials.matrix;
    if (s.projectMaterials.multiplier !== null) out.flush_multiplier = [s.projectMaterials.multiplier];
  }
  if (s.bedType) out.curr_bed_type = s.bedType;
  const plate = s.projectPlates.find((p) => p.index === s.plateIndex);
  const plateBed = plate?.settings.curr_bed_type;
  if (plateBed && !s.dirty.machine.curr_bed_type) out.curr_bed_type = plateBed;
  const tower = !s.project ? s.wipeTowerPos[s.plateIndex] : undefined;
  if (tower) [out.wipe_tower_x, out.wipe_tower_y] = [[tower[0]], [tower[1]]];
  if (plate && !s.project)
    for (const k of ['print_sequence', 'spiral_mode', 'first_layer_print_sequence', 'other_layers_print_sequence', 'other_layers_print_sequence_nums'])
      if (plate.settings[k] !== undefined) out[k] = plate.settings[k];
  if (s.filamentSlots.length > 1 || s.slotColours.some((c) => c !== undefined)) {
    const { matrix, multipliers } = allFlush(s);
    if (!s.project || s.slotColours.some((c) => c !== undefined))
      out.filament_colour = s.filamentSlots.map((_, i) => filamentColor(s, i + 1));
    if (!s.project || s.flush.matrix !== null) out.flush_volumes_matrix = matrix;
    out.flush_multiplier = multipliers;
  }
  if (Object.keys(s.mixes).length || MIX_KEYS.some((k) => k in out)) {
    const per = (f: (m: Mix) => string) => s.filamentSlots.map((_, i) => (s.mixes[i] ? f(s.mixes[i]) : ''));
    const flag = (f: (m: Mix) => boolean) => s.filamentSlots.map((_, i) => (s.mixes[i] && f(s.mixes[i]) ? '1' : '0'));
    const sum = (m: Mix) => m.ratios.reduce((a, b) => a + b, 0) || 100;
    Object.assign(out, {
      filament_is_mixed: flag(() => true),
      filament_mixed_components: per((m) => m.components.join(',')),
      filament_mixed_sublayer_ratios: per((m) => m.ratios.map((r) => Math.fround(r / sum(m)).toFixed(4)).join(',')),
      filament_mixed_gradient: flag((m) => m.gradient !== null),
      filament_mixed_gradient_range: per((m) => (m.gradient === null || m.components.length !== 2 ? '' : m.gradient ? '0.1000,0.9000' : '0.9000,0.1000')),
      filament_mixed_gradient_curve: per(() => ''),
      filament_mixed_gradient_per_part: flag((m) => m.gradient !== null && m.perPart),
    });
  }
  if (s.project && s.projectPlates.length > 1) for (const key of ['wipe_tower_x', 'wipe_tower_y']) {
    const value = out[key];
    if (Array.isArray(value) && value.length) out[key] = [Number(value[Math.min(s.plateIndex - 1, value.length - 1)])];
  }
  for (const k of Object.keys(out)) out[k] = jobValue(k, out[k]);
  if (s.project) for (const k of Object.keys(out)) {
    const values = OPTIONS[k].type === 'enum' ? OPTIONS[k].enum_values : undefined;
    if (values?.length && !values.includes(String(out[k]))) delete out[k];
  }
  return out;
}

export function calibOverrides(s = getState(), kind = ''): Config {
  const out: Config = {};
  const slots: ['machine' | 'process' | 'filament', string][] = [['machine', s.printer], ['process', s.process], ['filament', s.filamentSlots[0] ?? '']];
  for (const [type, name] of slots) Object.assign(out, resolvePreset(s, type, name).user?.overrides ?? {}, presetEdits(s, type));
  if (s.bedType) out.curr_bed_type = s.bedType;
  if (!kind.startsWith('nonplanar-')) out.nonplanar_mode = 'off';
  for (const k of Object.keys(out)) if (OPTIONS[k]) out[k] = jobValue(k, out[k]); else delete out[k];
  return out;
}

export function jobLibrary(s: State): JobLibrary {
  const id = (type: 'machine' | 'process' | 'filament', key: string) => resolvePreset(s, type, key).source?.id ?? '';
  return { printer: id('machine', s.printer), print: id('process', s.process), filaments: s.filamentSlots.map((f) => id('filament', f)), layerHeight: effectiveLayerHeight(s) };
}

export function appendLibrary(fd: FormData, s: State, settings: Config = {}) {
  const library = jobLibrary(s);
  if (!library.printer) throw new Error('Choose a printer first.');
  if (!library.print) throw new Error('Choose print settings first.');
  if (!library.filaments.length || library.filaments.some((f) => !f)) throw new Error('Choose a filament for every slot first.');
  fd.append('library', JSON.stringify({ ...library, project: canonicalModeConfig({ ...overrides(s), ...settings }) }));
}

export function jobFields(s: State, saving = false) {
  return {
    library: jobLibrary(s),
    ...(s.project && s.projectPlates.length > 1 ? { plate_index: s.plateIndex } : {}),
    ...(s.project ? { project_instances: projectInstances(s) } : {}),
    ...(s.project && canEditProject() && projectEdits(s).edits.length ? { project_edits: projectEdits(s).edits } : {}),
    overrides: overrides(saving ? { ...s, projectPlates: [] } : s),
    events: s.events.map(eventArg),
    output: outputKind(s),
    ...(s.mixedTemperatures && s.features.mixed_temperatures ? { allow_mixed_temperatures: true } : {}),
  };
}

export const firstLayer = (s: State) => Number(effectiveConfig(s, 'process').initial_layer_print_height) || 0.2;

export function plateSignature(s: State): string {
  return JSON.stringify({
    ...jobFields(s),
    resolved: [effectiveConfig(s, 'machine'), effectiveConfig(s, 'process'), ...s.filamentSlots.map((_, i) => effectiveConfig(s, 'filament', i))],
    plate: s.project ? undefined : plateDescription(s.objects, firstLayer(s)),
    files: s.project ? [fileId(s.project), ...projectEdits(s).added.map(meshSignature), ...projectEdits(s).volumes.map(({ o }) => meshSignature(o))] : s.objects.map(meshSignature),
  }, (key, value) => key === 'transform' && Array.isArray(value) ? value.map((v: number) => Math.round(v * 1e6) / 1e6)
    : value && typeof value === 'object' && !Array.isArray(value)
    ? Object.fromEntries(Object.entries(value).sort(([a], [b]) => a.localeCompare(b))) : value);
}

const fileId = (file: File) => `${file.name}:${file.size}:${file.lastModified}`;
