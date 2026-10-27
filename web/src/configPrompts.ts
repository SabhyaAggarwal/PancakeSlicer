import { confirmDialog, messageDialog } from './components/Ask';
import { undoable } from './state/history';
import { effectiveConfig, getState, markStale, presetEdits, setState } from './state/store';
import type { Config, ConfigValue, PresetType } from './api/types';

const TITLE = 'Warning';
const first = (v: ConfigValue | undefined) => Number(String(Array.isArray(v) ? v[0] : v ?? '').split(',')[0]);
const bool = (v: ConfigValue | undefined) => v === true || v === '1' || v === 'true' || v === 1;

function setOptions(type: PresetType, patch: Config, name: string, slot = 0) {
  undoable(name, () => setState((s) => {
    const edits = { ...presetEdits(s, type, slot), ...patch };
    return {
      ...(type === 'filament' ? { filamentEdits: { ...s.filamentEdits, [s.filamentSlots[slot]]: edits } } : { dirty: { ...s.dirty, [type]: edits } }),
      job: markStale(s.job),
    };
  }));
}

const SPIRAL: Config = { wall_loops: '1', top_shell_layers: '0', sparse_infill_density: '0%', enable_support: '0', enforce_support_layers: '0',
  detect_thin_wall: '0', overhang_reverse: '0', timelapse_type: '0', enable_wrapping_detection: '0' };
export const SPIRAL_KEYS = ['spiral_mode', ...Object.keys(SPIRAL)];

async function checkSpiral() {
  const s = getState(), c = effectiveConfig(s, 'process');
  if (!bool(c.spiral_mode)) return;
  const ok = first(c.wall_loops) === 1 && first(c.top_shell_layers) === 0 && parseFloat(String(c.sparse_infill_density)) === 0 && !bool(c.enable_support)
    && first(c.enforce_support_layers) === 0 && !bool(c.detect_thin_wall) && !bool(c.overhang_reverse) && String(c.timelapse_type ?? '0') === '0'
    && !bool(c.enable_wrapping_detection);
  if (ok) return;
  let msg = 'Spiral mode only works when wall loops is 1, support is disabled, clumping detection by probing is disabled, top shell layers is 0, sparse infill density is 0 and timelapse type is traditional.';
  if (effectiveConfig(s, 'machine').printer_structure === 'i3') msg += ' But machines with I3 structure will not generate timelapse videos.';
  msg += '\n\nChange these settings automatically?\nYes - Change these settings and enable spiral/vase mode automatically\nNo  - Cancel enabling spiral mode';
  if (await confirmDialog(TITLE, msg, ['Yes', 'No'])) setOptions('process', SPIRAL, 'Enable spiral vase');
  else setOptions('process', { spiral_mode: '0' }, 'Cancel spiral vase');
}

async function checkLayerHeight() {
  const s = getState(), m = effectiveConfig(s, 'machine');
  const list = (v: ConfigValue | undefined) => String(Array.isArray(v) ? v.join(',') : v ?? '').split(',').map(Number).filter(Number.isFinite);
  const mins = list(m.min_layer_height), maxs = list(m.max_layer_height);
  const min = mins.length ? Math.min(...mins) : 0, max = maxs.length ? Math.max(...maxs) : 0;
  const h = Number(effectiveConfig(s, 'process').layer_height), EPS = 1e-4;
  if (min > EPS && !(h >= EPS)) {
    await messageDialog(TITLE, `Layer height is too small. It will be set to the minimum (${min} mm).`);
    return setOptions('process', { layer_height: String(min) }, 'Change setting "Layer height"');
  }
  const to = max > EPS && h > max + EPS ? max : min > EPS && h < min - EPS ? min : null;
  if (to === null) return;
  const msg = 'Layer height is outside the limits set in Printer Settings -> Extruder -> Layer height limits, this may cause printing quality issues.'
    + `\n\nAdjust it to the limit (${to} mm) automatically?`;
  if (await confirmDialog(TITLE, msg, ['Adjust', 'Ignore'])) setOptions('process', { layer_height: String(to) }, 'Change setting "Layer height"');
}

async function checkNozzleTemperature(key: string, slot: number) {
  const c = effectiveConfig(getState(), 'filament', slot);
  const low = first(c.nozzle_temperature_range_low), high = first(c.nozzle_temperature_range_high), t = first(c[key]);
  if (!Number.isFinite(low) || !Number.isFinite(high) || !Number.isFinite(t) || (t >= low && t <= high)) return;
  await messageDialog(TITLE, 'The nozzle may become clogged when the temperature is out of the recommended range.\nPlease make sure whether to use this temperature to print.\n\n'
    + `The recommended nozzle temperature for this filament type is [${low}, ${high}] degrees Celsius.`);
}

export async function checkSetting(type: PresetType, key: string, slot = 0) {
  if (type === 'process' && SPIRAL_KEYS.includes(key)) return checkSpiral();
  if (type === 'process' && key === 'layer_height') return checkLayerHeight();
  if (type === 'filament' && (key === 'nozzle_temperature' || key === 'nozzle_temperature_initial_layer')) return checkNozzleTemperature(key, slot);
}
