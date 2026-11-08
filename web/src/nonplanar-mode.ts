import type { Config } from './api/types';

export const MODE_LABELS: Record<string, string> = {
  off: 'Flat layers', nonplanar_top: 'Smoothest tops', curved_tops: 'Fast curved tops', smooth_morph: 'Smooth morph',
};
export const MODE_HELP: Record<string, string> = {
  off: 'Flat layers: every layer flat. Fastest.',
  curved_tops: 'Fast curved tops: bends the outermost top layer where it adds no time. Other tops stay flat.',
  nonplanar_top: 'Smoothest tops: bends the top three layers over every gentle top. Smoothest finish, 10–50 % slower.',
  smooth_morph: 'Smooth morph: bends every layer from the bed up. Best for domes and lids, up to 1.4× slower.',
};
export const canonicalMode = (mode: string) => ['auto', 'manual', 'oneshot'].includes(mode) ? 'nonplanar_top' : mode;
export function canonicalModeConfig<T extends Config | Record<string, string>>(config: T): T {
  const mode = config.nonplanar_mode, value = Array.isArray(mode) ? String(mode[0] ?? '') : mode === undefined ? undefined : String(mode);
  if (value === undefined || canonicalMode(value) === value) return config;
  return { ...config, nonplanar_mode: Array.isArray(mode) ? [canonicalMode(value)] : canonicalMode(value) } as T;
}
