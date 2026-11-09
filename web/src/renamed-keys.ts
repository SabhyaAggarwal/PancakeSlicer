export const RENAMED_KEYS: Record<string, string> = { nonplanar_curved_shells: 'nonplanar_curved_top_layers' };

export function canonicalKeys<T extends Record<string, unknown>>(config: T): T {
  const old = Object.keys(RENAMED_KEYS).filter((key) => key in config);
  const mode = config.nonplanar_mode, fast = (Array.isArray(mode) ? mode[0] : mode) === 'curved_tops';
  if (!old.length && !fast) return config;
  const out: Record<string, unknown> = { ...config };
  for (const key of old) {
    if (!(RENAMED_KEYS[key] in out)) out[RENAMED_KEYS[key]] = out[key];
    delete out[key];
  }
  if (fast) {
    out.nonplanar_mode = Array.isArray(mode) ? ['nonplanar_top'] : 'nonplanar_top';
    out.nonplanar_curved_top_layers = '1';
  }
  return out as T;
}
