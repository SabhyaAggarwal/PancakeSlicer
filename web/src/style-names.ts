import type { Config, ConfigValue } from './api/types';
import { OPTIONS } from './options';

export type StyleId = 'auto' | 'standard' | 'smooth' | 'zaa' | 'multiplanar' | 'field' | 'oneshot';

export const STYLE_NAMES: Record<StyleId | 'custom', string> = {
  auto: 'Auto', standard: 'Standard', smooth: 'Smooth tops', zaa: 'Z contour',
  multiplanar: 'Tilted chunks', field: 'Rotary curved layers', oneshot: 'One-shot', custom: 'Custom',
};

const asText = (v: ConfigValue | undefined) => (Array.isArray(v) ? String(v[0] ?? '') : String(v ?? ''));
const on = (v: ConfigValue | undefined) => ['1', 'true'].includes(asText(v));

export function styleOf(c: Config): StyleId | 'custom' {
  const mode = asText(c.nonplanar_mode) || 'off';
  const zaa = on(c.zaa_enabled);
  if (mode === 'off') return zaa ? 'zaa' : 'standard';
  if (zaa) return 'custom';
  if (mode === 'auto') {
    const deform = asText(c.nonplanar_deform) || String(OPTIONS.nonplanar_deform?.default ?? 'auto');
    return deform === 'auto' ? 'auto' : deform === 'off' ? 'smooth' : 'custom';
  }
  if (mode === 'multiplanar' || mode === 'field' || mode === 'oneshot') return mode;
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
