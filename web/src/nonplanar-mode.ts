


import type { Config } from './api/types';

export const canonicalMode = (mode: string) => ['auto', 'manual', 'oneshot'].includes(mode) ? 'curved_tops' : mode;

export function canonicalModeConfig<T extends Config | Record<string, string>>(config: T): T {
  const mode = config.nonplanar_mode, value = Array.isArray(mode) ? String(mode[0] ?? '') : mode === undefined ? undefined : String(mode);
  if (value === undefined || canonicalMode(value) === value) return config;
  return { ...config, nonplanar_mode: Array.isArray(mode) ? [canonicalMode(value)] : canonicalMode(value) } as T;
}
