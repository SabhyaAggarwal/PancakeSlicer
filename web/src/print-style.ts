import type { Config, ConfigValue } from './api/types';
import { toolValue } from './config-values';
import { OPTIONS } from './options';
import { STYLE_NAMES, type StyleId } from './style-names';
import { undoable } from './state/history';
import { effectiveConfig, getState, removeObjectOptions, resetOption, resolvePreset, setObjectOption, setOption } from './state/store';

const STYLE_KEYS: Record<StyleId, Config> = {
  standard: { nonplanar_mode: 'off', zaa_enabled: '0' },
  smooth: { nonplanar_mode: 'nonplanar_top' },
  morph: { nonplanar_mode: 'smooth_morph' },
  zaa: { nonplanar_mode: 'off', zaa_enabled: '1' },
  multiplanar: { nonplanar_mode: 'multiplanar' },
  field: { nonplanar_mode: 'field' },
};

const same = (key: string, a: ConfigValue | undefined, b: ConfigValue | undefined) =>
  toolValue(key, a ?? (OPTIONS[key]?.default as ConfigValue) ?? '') === toolValue(key, b ?? (OPTIONS[key]?.default as ConfigValue) ?? '');

export function applyStyle(id: StyleId, objectId?: number) {
  undoable(`Change print style to "${STYLE_NAMES[id]}"`, () => writeStyle(id, objectId));
}

function writeStyle(id: StyleId, objectId?: number) {
  const s = getState();
  const keys = STYLE_KEYS[id];
  if (objectId === undefined) {
    const system = resolvePreset(s, 'process', s.process).config;
    for (const [k, v] of Object.entries(keys)) {
      if (same(k, v, system[k])) resetOption('process', k);
      else setOption('process', k, v);
    }
    return;
  }
  const global = effectiveConfig(s, 'process');
  const drop = Object.keys(keys).filter((k) => same(k, keys[k], global[k]));
  for (const [k, v] of Object.entries(keys)) if (!drop.includes(k)) setObjectOption(objectId, k, v);
  if (drop.length) removeObjectOptions(objectId, drop);
}
