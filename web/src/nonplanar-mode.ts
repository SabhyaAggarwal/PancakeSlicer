import type { Config } from './api/types';

export const MODE_LABELS: Record<string, string> = {
  off: 'Flat layers', nonplanar_top: 'Smoothest tops', curved_tops: 'Fast curved tops', smooth_morph: 'Smooth morph',
};
export const MODE_HELP: Record<string, string> = {
  off: 'Flat layers: every layer flat; fastest.',
  nonplanar_top: 'Smoothest tops: bends the top and two shells below over every gentle top; lowest top error, 10-50 % slower than flat.',
  curved_tops: 'Fast curved tops: bends only the outer top shell, and only where it stays near flat print time; other tops print flat.',
  smooth_morph: 'Smooth morph: bends every layer from the flat base up to the top; lowest error on domes and lids, up to about 1.4x flat time.',
};
export const canonicalMode = (mode: string) => ['auto', 'manual', 'oneshot'].includes(mode) ? 'nonplanar_top' : mode;
export function canonicalModeConfig<T extends Config | Record<string, string>>(config: T): T {
  const mode = config.nonplanar_mode, value = Array.isArray(mode) ? String(mode[0] ?? '') : mode === undefined ? undefined : String(mode);
  if (value === undefined || canonicalMode(value) === value) return config;
  return { ...config, nonplanar_mode: Array.isArray(mode) ? [canonicalMode(value)] : canonicalMode(value) } as T;
}
