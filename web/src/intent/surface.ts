

import type { Config, ConfigValue } from '../api/types';
import { canCurvedTops, canSurfaceWaves } from '../api/client';
import { OPTIONS } from '../options';
import { canonicalMode } from '../nonplanar-mode';
import { same, takeValue } from '../profiles/stack';
import { undoable } from '../state/history';
import { effectiveConfig, getState } from '../state/store';

export type SurfaceId = 'flat' | 'smooth' | 'waves';
export const SURFACE_NAMES = { flat: 'Flat layers', smooth: 'Curved layers', waves: 'Whole waves' } as const;
export const SMOOTH_COMING = 'Needs a newer slicing engine';
export const WAVES_COMING = 'Needs a newer slicing engine';
export const text = (v: ConfigValue | undefined) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');
const on = (v: ConfigValue | undefined) => ['1', 'true'].includes(text(v));

export const CURVED_DEFAULTS: Config = { nonplanar_curved_max_angle: '12', nonplanar_curved_max_height: '5', nonplanar_curved_shells: '3', nonplanar_curved_cone_tolerance: '0.05' };
export const WAVE_DEFAULTS: Config = { nonplanar_wave_amplitude: '0.4', nonplanar_wave_wavelength: '30', nonplanar_wave_direction: '45' };
export const SURFACE_KEYS = ['nonplanar_mode', 'nonplanar_wave', 'nonplanar_deform', 'zaa_enabled'];

export function surfaceOf(config: Config): SurfaceId | 'custom' {
  const mode = canonicalMode(text(config.nonplanar_mode) || 'off');
  const wave = text(config.nonplanar_wave) || 'off';
  if (on(config.zaa_enabled)) return 'custom';
  if (wave !== 'off') return ['top', 'whole', 'infill'].includes(wave) && mode === 'off' ? 'waves' : 'custom';
  if (mode === 'curved_tops') return 'smooth';
  return mode === 'off' ? 'flat' : 'custom';
}

export function surfaceUnavailable(id: SurfaceId): string | undefined {
  if (id === 'smooth' && (!canCurvedTops() || !OPTIONS.nonplanar_mode?.enum_values?.includes('curved_tops'))) return SMOOTH_COMING;
  if (id === 'waves' && (!canSurfaceWaves() || !OPTIONS.nonplanar_wave?.enum_values?.includes('whole'))) return WAVES_COMING;
}


export function surfaceValues(id: SurfaceId): Config {
  return {
    nonplanar_mode: id === 'smooth' ? 'curved_tops' : 'off',
    nonplanar_deform: 'off', zaa_enabled: '0',
    ...(OPTIONS.nonplanar_time_budget ? { nonplanar_time_budget: id === 'smooth' ? '40%' : '15%' } : {}),
    ...(OPTIONS.nonplanar_wave ? { nonplanar_wave: id === 'waves' ? 'whole' : 'off' } : {}),
  };
}


export function writeSurface(id: SurfaceId) {
  if (surfaceUnavailable(id)) return;
  const config = effectiveConfig(getState(), 'process');
  const defaults = id === 'smooth' ? CURVED_DEFAULTS : id === 'waves' ? WAVE_DEFAULTS : {};
  for (const [key, value] of Object.entries(defaults)) if (OPTIONS[key] && (config[key] === undefined || Number(text(config[key])) === 0)) takeValue('process', key, value);
  for (const [key, value] of Object.entries(surfaceValues(id))) if (!same(key, config[key], value)) takeValue('process', key, value);
}

export function applySurface(id: SurfaceId) {
  undoable(`Surface: ${SURFACE_NAMES[id]}`, () => writeSurface(id));
}



export function keepCurvedTop(extraPercent: number) {
  undoable('Keep curved top', () => {
    writeSurface('smooth');
    const needed = Math.ceil(extraPercent) + 5;
    if (OPTIONS.nonplanar_time_budget && needed > 40) takeValue('process', 'nonplanar_time_budget', `${needed}%`);
  });
}
