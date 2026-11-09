import type { Config, ConfigValue } from './api/types';
import { MODE_LABELS } from './nonplanar-mode';

export type StyleId = 'standard' | 'smooth' | 'fast' | 'morph' | 'zaa' | 'multiplanar' | 'field';

export type StyleName = StyleId | 'slant' | 'conformal' | 'custom';

export const STYLE_NAMES: Record<StyleName, string> = {
  standard: 'Standard', smooth: MODE_LABELS.nonplanar_top, fast: MODE_LABELS.curved_tops, morph: MODE_LABELS.smooth_morph, zaa: 'Z contour',
  multiplanar: 'Tilted chunks', field: 'Rotary curved layers', slant: 'Slant', conformal: 'Conformal layers', custom: 'Custom',
};

const asText = (v: ConfigValue | undefined) => (Array.isArray(v) ? String(v[0] ?? '') : String(v ?? ''));
const on = (v: ConfigValue | undefined) => ['1', 'true'].includes(asText(v));

export function styleOf(c: Config): StyleName {
  const mode = asText(c.nonplanar_mode) || 'off';
  const zaa = on(c.zaa_enabled);
  if (mode === 'off') return zaa ? 'zaa' : 'standard';
  if (zaa) return 'custom';
  if (mode === 'nonplanar_top') return 'smooth';
  if (mode === 'curved_tops') return 'fast';
  if (mode === 'smooth_morph') return 'morph';
  if (mode === 'multiplanar' || mode === 'field') return mode;
  if (mode === 'slant') return 'slant';
  if (mode === 'conformal_layers') return 'conformal';
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
