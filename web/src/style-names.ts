import type { Config, ConfigValue } from './api/types';
import { canonicalMode } from './nonplanar-mode';

export type StyleId = 'standard' | 'smooth' | 'zaa' | 'multiplanar' | 'field';

export const STYLE_NAMES: Record<StyleId | 'custom', string> = {
  standard: 'Standard', smooth: 'Curved tops', zaa: 'Z contour',
  multiplanar: 'Tilted chunks', field: 'Rotary curved layers', custom: 'Custom',
};

const asText = (v: ConfigValue | undefined) => (Array.isArray(v) ? String(v[0] ?? '') : String(v ?? ''));
const on = (v: ConfigValue | undefined) => ['1', 'true'].includes(asText(v));

export function styleOf(c: Config): StyleId | 'custom' {
  const mode = canonicalMode(asText(c.nonplanar_mode) || 'off');
  const zaa = on(c.zaa_enabled);
  if (mode === 'off') return zaa ? 'zaa' : 'standard';
  if (zaa) return 'custom';
  if (mode === 'curved_tops') return 'smooth';
  if (mode === 'multiplanar' || mode === 'field') return mode;
  return 'custom';
}

type RecordObject = { mode?: unknown; deform?: unknown };

export function recordConfig(inputs: Record<string, unknown>, objects: RecordObject[] = []): Config {
  const object = objects.find((o) => o.mode && o.mode !== 'off');
  const override = (Array.isArray(inputs.overrides) ? inputs.overrides : [])
    .map((o) => (Array.isArray(o) ? o : [(o as { key?: unknown })?.key, (o as { value?: unknown })?.value]))
    .filter(([k]) => k === 'nonplanar_deform').pop()?.[1];
  const deform = (object ? object.deform : undefined) ?? inputs.deform ?? override;
  return {
    nonplanar_mode: String(object?.mode ?? inputs.mode ?? 'off'),
    zaa_enabled: inputs.zaa_enabled === true ? '1' : '0',
    ...(deform != null ? { nonplanar_deform: String(deform) } : {}),
  };
}

export const styleName = (c: Config) => STYLE_NAMES[styleOf(c)];
