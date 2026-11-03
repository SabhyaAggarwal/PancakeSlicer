import type { Config, ConfigValue, PresetType, ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import { normalizeConfig } from '../config-values';
import { sameValue } from '../state/value-equality';
import { OPTIONS } from '../options';

export const PRESET_META = /^(inherits|name|from|version|type|setting_id|base_id|filament_id|is_custom_defined|instantiation|\w+_settings_id)$/;
export interface ProfileCatalog { machines: SystemPreset[]; processes: SystemPreset[]; filaments: SystemPreset[]; userPresets: UserPreset[] }
export interface ProfileConflict { key: string; mine: ConfigValue; theirs: ConfigValue | undefined }
export const profilesFor = (s: ProfileCatalog, type: PresetType) => type === 'machine' ? s.machines : type === 'process' ? s.processes : s.filaments;

export function baseVersion(p: SystemPreset): string {
  if (p.version || p.config.version) return String(p.version || p.config.version);
  const text = JSON.stringify(Object.entries(p.config).sort(([a], [b]) => a.localeCompare(b)));
  let hash = 2166136261;
  for (let i = 0; i < text.length; i++) hash = Math.imul(hash ^ text.charCodeAt(i), 16777619);
  return `content:${(hash >>> 0).toString(16)}`;
}

export const changedKeys = (values: Config, below: Config): Config => Object.fromEntries(
  Object.entries(values).filter(([key, value]) => !PRESET_META.test(key) && !sameValue(key, value, below[key] ?? OPTIONS[key]?.default as ConfigValue | undefined)),
);

export function normalizeUserPreset(p: UserPreset | ProfileOverlay): UserPreset {
  const legacy = p as UserPreset;
  return { ...p, inherits: p.base?.name ?? legacy.inherits ?? '',
    overrides: normalizeConfig(legacy.overrides ?? p.overlay ?? {}),
    ...(p.overlay ? { overlay: normalizeConfig(p.overlay) } : {}) };
}

export function resolveProfile(s: ProfileCatalog, type: PresetType, name: string, seen = new Set<string>()): {
  base?: SystemPreset; user?: UserPreset; config: Config;
} {
  if (seen.has(name)) return { config: {} };
  seen.add(name);
  const user = s.userPresets.find((u) => u.type === type && u.name === name);
  if (!user) {
    const base = profilesFor(s, type).find((p) => p.name === name);
    return { base, config: { ...base?.config } };
  }
  const parentName = user.parent ?? (!user.base && s.userPresets.some((u) => u.type === type && u.name === user.inherits) ? user.inherits : undefined);
  const parent = parentName ? resolveProfile(s, type, parentName, seen) : undefined;
  const base = profilesFor(s, type).find((p) => p.name === (user.base?.name ?? user.inherits)) ?? parent?.base;
  const config = { ...base?.config, ...parent?.config, ...(user.overlay ?? user.overrides) };
  return { base, user: parentName ? { ...user, overrides: { ...parent?.user?.overrides, ...(user.overlay ?? user.overrides) } } : user, config };
}

export function prepareProfile(s: ProfileCatalog, input: UserPreset): UserPreset {
  const p = normalizeUserPreset(input);
  if (p.base && p.overlay && p.baseline) return p;
  const resolved = resolveProfile(s, p.type, p.inherits);
  if (!resolved.base) return p;
  const base = resolved.base;
  const overlay = changedKeys(p.overrides, resolved.config);
  const metadata: Config = { ...p.metadata };
  for (const [key, value] of Object.entries(p.overrides)) if (PRESET_META.test(key)) metadata[key] = value;
  return { ...p, base: { name: base.name, version: baseVersion(base) },
    parent: resolved.user ? p.inherits : undefined, inherits: base.name, overlay, overrides: overlay,
    baseline: Object.fromEntries(Object.keys(overlay).map((key) => [key, base.config[key] ?? null])), metadata };
}

export function profileConflicts(s: ProfileCatalog, type: PresetType, name: string): ProfileConflict[] {
  const { base, user } = resolveProfile(s, type, name);
  if (!base || !user?.base || !user.overlay || !user.baseline || user.base.version === baseVersion(base)) return [];
  return Object.entries(user.overlay).flatMap(([key, mine]) =>
    key in user.baseline! && !sameValue(key, user.baseline![key] ?? undefined, base.config[key]) && !sameValue(key, mine, base.config[key])
      ? [{ key, mine, theirs: base.config[key] }] : []);
}

export function saveProfile(s: ProfileCatalog, type: PresetType, name: string, current: string, values: Config): UserPreset {
  const resolved = resolveProfile(s, type, current);
  const prior = resolved.user && prepareProfile(s, resolved.user);
  const base = resolved.base;
  if (!base) return { name, type, inherits: prior?.inherits ?? current, overrides: values };
  let parent = name !== current && prior ? current : prior?.parent;
  const ancestors = new Set<string>();
  for (let ancestor = parent; ancestor && !ancestors.has(ancestor);) {
    if (ancestor === name) { parent = undefined; break; }
    ancestors.add(ancestor);
    const profile = s.userPresets.find((p) => p.type === type && p.name === ancestor);
    ancestor = profile?.parent ?? (!profile?.base ? profile?.inherits : undefined);
  }
  const below = parent ? resolveProfile(s, type, parent).config : base.config;
  const overlay = changedKeys(values, below);
  const baseline = Object.fromEntries(Object.keys(overlay).map((key) => [key,
    name === current && prior?.overlay && sameValue(key, overlay[key], prior.overlay[key]) && key in (prior.baseline ?? {})
      ? prior.baseline![key] : base.config[key] ?? null]));
  return { name, type, inherits: base.name, base: name === current && prior?.base ? prior.base : { name: base.name, version: baseVersion(base) },
    parent, overlay, overrides: overlay, baseline, metadata: prior?.metadata };
}

export function decideConflict(s: ProfileCatalog, type: PresetType, name: string, key: string, choice: 'mine' | 'theirs'): UserPreset | undefined {
  const { base, user } = resolveProfile(s, type, name);
  if (!base || !user?.overlay) return undefined;
  const overlay = { ...user.overlay }, baseline = { ...user.baseline, [key]: base.config[key] ?? null };
  if (choice === 'theirs') {
    const below = user.parent ? resolveProfile(s, type, user.parent).config : base.config;
    if (base.config[key] === undefined || sameValue(key, below[key], base.config[key])) { delete overlay[key]; delete baseline[key]; }
    else if (base.config[key] !== undefined) overlay[key] = base.config[key];
  }
  return { ...user, overlay, overrides: overlay, baseline };
}

export function storedProfile(p: UserPreset): ProfileOverlay {
  return { name: p.name, type: p.type, base: p.base ?? { name: p.inherits, version: '' }, parent: p.parent,
    overlay: p.overlay ?? p.overrides, baseline: p.baseline, metadata: p.metadata };
}

export function preserveProfileParents(previous: ProfileCatalog, next: ProfileCatalog): UserPreset[] {
  const removed = previous.userPresets.filter((p) => !next.userPresets.some((n) => n.type === p.type && n.name === p.name));
  if (!removed.length) return next.userPresets;
  const added = next.userPresets.filter((p) => !previous.userPresets.some((n) => n.type === p.type && n.name === p.name));
  return next.userPresets.map((p) => {
    const lost = removed.find((parent) => parent.type === p.type && parent.name === p.parent);
    if (!lost) return p;
    const replacement = added.find((candidate) => candidate.type === lost.type && candidate.base?.name === lost.base?.name
      && candidate.parent === lost.parent && !Object.keys(changedKeys(candidate.overlay ?? candidate.overrides, lost.overlay ?? lost.overrides)).length
      && !Object.keys(changedKeys(lost.overlay ?? lost.overrides, candidate.overlay ?? candidate.overrides)).length);
    if (replacement) return { ...p, parent: replacement.name };
    const before = resolveProfile(previous, lost.type, lost.name);
    const base = resolveProfile(next, p.type, p.name).base ?? before.base;
    const overlay = changedKeys({ ...before.user?.overrides, ...p.overlay ?? p.overrides }, base?.config ?? {});
    return { ...p, parent: undefined, overlay, overrides: overlay,
      baseline: Object.fromEntries(Object.keys(overlay).map((key) => [key, p.baseline?.[key] ?? before.user?.baseline?.[key] ?? base?.config[key] ?? null])) };
  });
}
