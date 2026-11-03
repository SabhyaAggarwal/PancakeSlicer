import type { Config, ConfigValue, PresetType, ProfileOverlay, SystemPreset, UserPreset } from '../api/types';
import { normalizeConfig } from '../config-values';
import { sameValue } from '../state/value-equality';
import { OPTIONS } from '../options';
import { findProfile, lookupProfile, profileKey, withProfileIdentity } from './identity';
import type { ProfileBaseAnchor, ProfileRecoveryReason } from './ownership';

export const PRESET_META = /^(inherits|name|from|version|type|setting_id|base_id|filament_id|is_custom_defined|instantiation|\w+_settings_id)$/;
export interface ProfileCatalog { machines: SystemPreset[]; processes: SystemPreset[]; filaments: SystemPreset[]; userPresets: UserPreset[] }
export interface ProfileConflict { key: string; mine: ConfigValue; theirs: ConfigValue | undefined }
export const profilesFor = (s: ProfileCatalog, type: PresetType) => type === 'machine' ? s.machines : type === 'process' ? s.processes : s.filaments;

const contentVersions = new WeakMap<Config, string>();
export function baseVersion(p: SystemPreset): string {
  if (p.version || p.config.version) return String(p.version || p.config.version);
  const cached = contentVersions.get(p.config);
  if (cached) return cached;
  const text = JSON.stringify(Object.entries(p.config).sort(([a], [b]) => a.localeCompare(b)));
  let hash = 2166136261;
  for (let i = 0; i < text.length; i++) hash = Math.imul(hash ^ text.charCodeAt(i), 16777619);
  const version = `content:${(hash >>> 0).toString(16)}`;
  contentVersions.set(p.config, version);
  return version;
}


export const catalogAnchor = (base: SystemPreset): ProfileBaseAnchor => ({ name: base.name, version: baseVersion(base), vendor: base.vendor, ...(base.setting_id ? {id: base.setting_id} : {}), engineName: base.engine_base ?? base.name });

export const changedKeys = (values: Config, below: Config): Config => Object.fromEntries(
  Object.entries(values).filter(([key, value]) => !PRESET_META.test(key) && !sameValue(key, value, below[key] ?? OPTIONS[key]?.default as ConfigValue | undefined)),
);

export function normalizeUserPreset(p: UserPreset | ProfileOverlay): UserPreset {
  const legacy = p as UserPreset;
  return { ...withProfileIdentity(p as UserPreset), revision: p.revision ?? 0, kind: p.kind ?? 'profile', inherits: p.base?.name ?? legacy.inherits ?? '',
    overrides: normalizeConfig(legacy.overrides ?? p.overlay ?? {}),
    ...(p.overlay ? { overlay: normalizeConfig(p.overlay) } : {}) };
}

export interface ProfileResolutionIssue { code: ProfileRecoveryReason; key: string; message: string }
export interface ResolvedProfile {
  base?: SystemPreset;
  user?: UserPreset;
  source?: UserPreset;
  key?: string;
  name?: string;
  engineName?: string;
  config: Config;
  status: 'ready' | 'snapshot' | ProfileRecoveryReason;
  issue?: ProfileResolutionIssue;
  error?: string;
}
export class ProfileResolutionError extends Error {
  readonly code: ProfileRecoveryReason;
  constructor(readonly issue: ProfileResolutionIssue) { super(issue.message); this.name = 'ProfileResolutionError'; this.code = issue.code; }
}
const unavailable = (code: ProfileRecoveryReason, key: string, message: string, source?: UserPreset): ResolvedProfile => ({
  key, name: source?.name, source, user: source, config: source?.snapshot?.config ?? {},
  status: source?.snapshot ? 'snapshot' : code, issue: { code, key, message }, error: message,
});
export function findCatalogBase(s: ProfileCatalog, type: PresetType, anchor: string | ProfileBaseAnchor): { base?: SystemPreset; ambiguous: boolean } {
  const ref: Partial<ProfileBaseAnchor> & {name: string} = typeof anchor === 'string' ? { name: anchor } : anchor;
  const catalog = profilesFor(s, type).filter(p => !ref.vendor || p.vendor === ref.vendor);
  let exact = catalog.filter(p => p.name === ref.name);
  if (exact.length > 1 && ref.id) exact = exact.filter(p => p.setting_id === ref.id || p.base_id === ref.id);
  if (exact.length) return { base: exact.length === 1 ? exact[0] : undefined, ambiguous: exact.length > 1 };
  const renamed = catalog.filter(p => {
    const field = p.config.renamed_from;
    const aliases = Array.isArray(field) ? field.map(String) : field === undefined ? [p.name.replace('@', '')] : String(field).split(';').map(n => n.replace(/^ +/, ''));
    return aliases.includes(ref.name);
  });
  return { base: renamed.length === 1 ? renamed[0] : undefined, ambiguous: renamed.length > 1 };
}
const resolvedProfiles = new WeakMap<SystemPreset[], WeakMap<UserPreset[], Map<string, ResolvedProfile>>>();

export function resolveProfile(s: ProfileCatalog, type: PresetType, name: string, seen?: Set<string>): ResolvedProfile {
  if (seen) return resolveAncestors(s, type, name, seen);
  const catalog = profilesFor(s, type);
  let libraries = resolvedProfiles.get(catalog);
  if (!libraries) resolvedProfiles.set(catalog, (libraries = new WeakMap()));
  let values = libraries.get(s.userPresets);
  if (!values) libraries.set(s.userPresets, (values = new Map()));
  const key = `${type}:${name}`, cached = values.get(key);
  if (cached) return cached;
  const resolved = resolveAncestors(s, type, name, new Set());
  if (resolved.base || resolved.user) values.set(key, resolved);
  return resolved;
}

function resolveAncestors(s: ProfileCatalog, type: PresetType, name: string, seen: Set<string>): ResolvedProfile {
  const lookup = lookupProfile(s, type, name), user = lookup.profile;
  const identity = user ? profileKey(user) : name;
  if (seen.has(identity)) return unavailable('cycle', identity, 'Profile inheritance contains a cycle', user);
  seen.add(identity);
  if (!user) {
    if (lookup.ambiguous)
      return unavailable('ambiguous', name, 'More than one profile uses this reference. Choose its exact profile.');
    const result = findCatalogBase(s, type, name), base = result.base;
    if (!base) return unavailable(result.ambiguous ? 'ambiguous' : 'missing-profile', name, result.ambiguous ? 'The catalog reference is ambiguous.' : 'This profile is unavailable. Reimport it or recover its saved settings.');
    return { base, name: base.name, engineName: base.engine_base ?? base.name, config: base.config, status: 'ready' };
  }
  const parentName = user.parentId ?? user.parent ?? (!user.base && findProfile(s, type, user.inherits) ? user.inherits : undefined);
  const parent = parentName ? resolveAncestors(s, type, parentName, seen) : undefined;
  if (parent?.issue && parent.status !== 'archived' || parentName && !parent?.source)
    return unavailable(parent?.issue?.code ?? 'missing-profile', identity, parent?.error ?? 'The parent profile is unavailable. Recover its saved settings.', user);
  const found = findCatalogBase(s, type, user.base ?? user.inherits);
  const base = found.base ?? (!user.base ? parent?.base : undefined);
  if (!base) return unavailable(found.ambiguous ? 'ambiguous' : 'missing-base', identity, 'The profile base is unavailable. Reimport the base or recover a copy of its saved settings.', user);
  if (parent?.base && (parent.base.name !== base.name || parent.base.vendor !== base.vendor))
    return unavailable('missing-base', identity, 'The parent profile has a different base. Recover a copy before editing.', user);
  let master: ResolvedProfile | undefined;
  if (user.kind !== 'master' && !parentName) {
    const candidates = user.masterId ? [findProfile(s,type,user.masterId)].filter((p): p is UserPreset => !!p)
      : s.userPresets.filter(p => p.type === type && p.kind === 'master' && p.base?.name === base.name && (!p.base.vendor || p.base.vendor === base.vendor));
    if (candidates.length > 1) return unavailable('ambiguous', identity, 'Choose the exact local master for this profile.', user);
    if (user.masterId && !candidates.length) return unavailable('missing-profile', identity, 'The linked master is unavailable. Recover the saved settings.', user);
    if (candidates[0]) {
      master = resolveAncestors(s,type,profileKey(candidates[0]),seen);
      if (master.issue && master.status !== 'archived') return unavailable(master.issue.code,identity,master.error!,user);
    }
  }
  const constraints = Object.fromEntries(Object.entries(user.metadata ?? {}).filter(([key]) => /^compatible_(printers|prints)(_condition)?$/.test(key)));
  const owned = { ...master?.user?.overrides, ...parent?.user?.overrides, ...constraints, ...(user.overlay ?? user.overrides) };
  const pinned = type === 'filament' && user.printerId && user.snapshot && user.base?.version !== baseVersion(base);
  const config = pinned ? { ...user.snapshot!.config, ...(user.overlay ?? user.overrides) } : { ...base.config, ...owned };
  if (user.archived) {
    const retained = user.snapshot?.config ?? config;
    return {base,source:user,user:{...user,overrides:changedKeys(retained,base.config)},key:identity,name:user.name,config:retained,status:'archived',
      issue:{code:'archived',key:identity,message:'This profile was removed from the library. Undo or recover a copy before using it.'},
      error:'This profile was removed from the library. Undo or recover a copy before using it.'};
  }
  return { base, source: user, user: parentName || master ? { ...user, overrides: owned } : user, key: identity, name: user.name,
    engineName: base.engine_base ?? base.name, config, status: 'ready' };
}

export function prepareProfile(s: ProfileCatalog, input: UserPreset): UserPreset {
  const p = normalizeUserPreset(input);
  if (p.base && p.overlay && p.baseline) {
    const base=findCatalogBase(s,p.type,p.base).base;
    const engineName=base?.engine_base ?? base?.name;
    return engineName && p.base.engineName!==engineName ? {...p,base:{...p.base,engineName}} : p;
  }
  const resolved = resolveProfile(s, p.type, p.inherits);
  if (!resolved.base) return p;
  const base = resolved.base;
  const overlay = changedKeys(p.overrides, resolved.config);
  const metadata: Config = { ...p.metadata };
  for (const [key, value] of Object.entries(p.overrides)) if (PRESET_META.test(key)) metadata[key] = value;
  return { ...p, base: catalogAnchor(base), snapshot: {config: { ...resolved.config, ...p.overrides }, base: catalogAnchor(base)},
    parentId: resolved.source ? profileKey(resolved.source) : undefined, parent: resolved.user ? p.inherits : undefined, inherits: base.name, overlay, overrides: overlay,
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
  if (resolved.issue) throw new ProfileResolutionError(resolved.issue);
  const prior = resolved.source && prepareProfile(s, resolved.source), base = resolved.base!;
  const same = !!prior && (name === current || name === prior.name || name === profileKey(prior));
  const label = same ? prior!.name : name;
  let parentId = same ? prior?.parentId : prior ? profileKey(prior) : undefined;
  let parent = same ? prior?.parent : prior?.name;
  const destination = findProfile(s, type, label), ancestors = new Set<string>();
  for (let key = parentId ?? parent; key && !ancestors.has(key);) {
    const ancestor = findProfile(s, type, key);
    if (destination && ancestor && profileKey(destination) === profileKey(ancestor)) { parent = undefined; parentId = undefined; break; }
    ancestors.add(key); key = ancestor?.parentId ?? ancestor?.parent;
  }
  const linkedMaster = prior?.masterId ? findProfile(s,type,prior.masterId) : type && s.userPresets.find(p => p.type === type && p.kind === 'master' && p.base?.name === base.name && (!p.base.vendor || p.base.vendor === base.vendor));
  const below = parentId || parent ? resolveProfile(s, type, parentId ?? parent!).config : prior?.kind !== 'master' && linkedMaster ? resolveProfile(s,type,profileKey(linkedMaster)).config : base.config;
  const overlay = changedKeys(values, below);
  const baseline = Object.fromEntries(Object.keys(overlay).map(key => [key,
    same && prior?.overlay && sameValue(key, overlay[key], prior.overlay[key]) && key in (prior.baseline ?? {})
      ? prior.baseline![key] : base.config[key] ?? null]));
  return { ...(same ? prior : undefined), id: same ? profileKey(prior!) : `profile:${crypto.randomUUID()}`,
    name: label, type, kind: prior?.kind ?? 'profile', revision: same ? (prior?.revision ?? 0) + 1 : 0,
    printerId: prior?.printerId, inherits: base.name, base: same && prior?.base ? prior.base : catalogAnchor(base),
    snapshot: { config: { ...values }, base: same && prior?.base ? prior.base : catalogAnchor(base) },
    parentId, parent, overlay, overrides: overlay, baseline, metadata: prior?.metadata };

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
  const { inherits, overrides, ...profile } = withProfileIdentity(p);
  return { ...profile, revision: p.revision ?? 0, kind: p.kind ?? 'profile',
    name: p.name, type: p.type, base: p.base ?? { name: inherits, version: '' },
    overlay: p.overlay ?? overrides, baseline: p.baseline, metadata: p.metadata };
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
