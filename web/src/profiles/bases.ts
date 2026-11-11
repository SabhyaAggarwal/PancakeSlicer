import { speedModelOf, SPEED_KEYS } from './speed-rules';
import type { PresetType } from '../api/types';
import type { State } from '../state/store';
import { resolveProfile } from './model';
import { originsOf, printerOverrides, type SettingOrigin } from './context';

export type { SettingOrigin };
export type SettingSource = SettingOrigin;
const sourceCache = new WeakMap<State, Map<string, Record<string, SettingOrigin>>>();
export function settingSources(s: State, type: PresetType, key: string): Record<string, SettingOrigin> {
  let byProfile = sourceCache.get(s);
  if (!byProfile) sourceCache.set(s, (byProfile = new Map()));
  const id = `${type}:${key}`, hit = byProfile.get(id);
  if (hit) return hit;
  const out = type === 'process' ? { ...originsOf(resolveProfile(s, type, key).user) } : {};
  if (type === 'process' && key === s.process) {
    const project = s.projectPresets?.process ?? {}, plate = s.projectPlates?.find((p) => p.index === s.plateIndex)?.settings ?? {};
    for (const k of Object.keys(printerOverrides(s))) if (!(k in project) && !(k in plate) && !(k in s.dirty.process) && !(speedModelOf(resolveProfile(s, type, key).source) && SPEED_KEYS.has(k))) out[k] = 'printer';
  }
  byProfile.set(id, out);
  return out;
}
