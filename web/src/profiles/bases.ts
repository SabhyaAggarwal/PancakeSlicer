import type { Config, PresetType, UserPreset } from '../api/types';
import { getState, resolvePreset, type State } from '../state/store';
import { OPTIONS } from '../options';
import { findProfile, profileKey } from './identity';
import { resolveProfile, PRESET_META } from './model';
import { originsOf, profileChain, type SettingOrigin } from './context';
import { foldBase, resetToBase, type ChainLink } from './chain';

export type BaseRole = 'base' | 'base-only';
export const baseRoleOf = (p: UserPreset | undefined) => (p as { baseRole?: BaseRole } | undefined)?.baseRole;
const live = (p: UserPreset) => !p.archived && p.kind !== 'master';
const record = (s: State, type: PresetType, key: string) => {
  const p = findProfile(s, type, key);
  if (!p) throw new Error('This profile is no longer in the library.');
  return p;
};
const anchorOf = (p: UserPreset) => `${p.base?.name ?? p.inherits}|${p.base?.version ?? ''}`;

export const childrenOf = (s: State, type: PresetType, key: string) => s.userPresets.filter((p) => p.type === type && live(p) && p.parentId === key);
export const baseOf = (s: State, type: PresetType, key: string) => { const p = findProfile(s, type, key); return p?.parentId ? findProfile(s, type, p.parentId) : undefined; };
const depth = (s: State, type: PresetType, key: string) => profileChain(s, type, resolveProfile(s, type, key)).length;
const subtreeDepth = (s: State, type: PresetType, key: string): number =>
  1 + Math.max(0, ...childrenOf(s, type, key).map((c) => subtreeDepth(s, type, profileKey(c))));

export function baseCandidates(s: State, type: PresetType, key?: string): UserPreset[] {
  const self = key ? record(s, type, key) : undefined, below = key ? subtreeDepth(s, type, key) : 1;
  return s.userPresets.filter((p) => p.type === type && live(p) && baseRoleOf(p)
    && (!self || profileKey(p) !== key && anchorOf(p) === anchorOf(self)
      && !profileChain(s, type, resolveProfile(s, type, profileKey(p))).some((l) => l.id === key))
    && depth(s, type, profileKey(p)) + below <= 4)
    .sort((a, b) => a.name.localeCompare(b.name));
}

const linkOf = (p: UserPreset, type: PresetType): ChainLink => ({ ...(p as unknown as ChainLink), type, overlay: p.overlay ?? p.overrides ?? {} });
function storedRecord(s: State, type: PresetType, p: UserPreset, link: ChainLink): UserPreset {
  const root = resolvePreset(s, type, profileKey(p)).base?.config ?? {}, overlay = link.overlay;
  const baseline = Object.fromEntries(Object.keys(overlay).map((k) => [k, p.baseline?.[k] ?? root[k] ?? null]));
  return { ...(link as unknown as UserPreset), overlay, overrides: overlay, baseline } as UserPreset;
}
const commit = async (records: UserPreset[], operationId?: string) => (await import('./transactions')).commitProfileRecords(records, { operationId });

export async function setBaseRole(type: PresetType, key: string, role: BaseRole | undefined) {
  const p = record(getState(), type, key), next = { ...p } as UserPreset & { baseRole?: BaseRole };
  if (role) next.baseRole = role; else delete next.baseRole;
  await commit([next]);
}

export async function createBasedOn(type: PresetType, parentKey: string, name: string): Promise<string> {
  const { profileCreation } = await import('./transactions');
  return profileCreation((st) => {
    const parent = record(st, type, parentKey);
    if (st.userPresets.some((p) => p.type === type && p.name === name && !p.archived)) throw new Error('A profile already uses this name. Choose another name.');
    return { id: `profile:${crypto.randomUUID()}`, type, kind: 'profile', name, revision: 0, base: parent.base, inherits: parent.inherits,
      parentId: profileKey(parent), overlay: {}, overrides: {}, baseline: {}, aliases: [] } as unknown as UserPreset;
  }).commit();
}

export interface ValueChange { key: string; label: string; from: string; to: string }
const shown = (v: Config[string] | undefined) => v === undefined ? '—' : Array.isArray(v) ? v.join(', ') : String(v);
export function changeBasePreview(s: State, type: PresetType, key: string, parentKey: string | null): ValueChange[] {
  const p = record(s, type, key), moved = { ...p, parentId: parentKey ?? undefined } as UserPreset;
  const next = { ...s, userPresets: s.userPresets.map((x) => (x === p ? moved : x)) } as State;
  const before = resolvePreset(s, type, key).config, after = resolvePreset(next, type, key).config;
  return [...new Set([...Object.keys(before), ...Object.keys(after)])].filter((k) => !PRESET_META.test(k) && shown(before[k]) !== shown(after[k]))
    .map((k) => ({ key: k, label: OPTIONS[k]?.label || k, from: shown(before[k]), to: shown(after[k]) })).sort((a, b) => a.label.localeCompare(b.label));
}
export async function changeBase(type: PresetType, key: string, parentKey: string | null) {
  const p = record(getState(), type, key), next = { ...p } as UserPreset;
  if (parentKey) next.parentId = parentKey; else delete next.parentId;
  await commit([next]);
}

export async function archiveBases(type: PresetType, keys: string[]) {
  const s = getState(), operationId = `archive:${crypto.randomUUID()}`, { archiveProfile } = await import('./library-actions');
  const moved = new Map<string, UserPreset>();
  for (const key of keys) {
    const base = record(s, type, key);
    for (const child of childrenOf(s, type, key)) {
      const current = moved.get(profileKey(child)) ?? child, link = { ...foldBase(linkOf(base, type), linkOf(current, type)) };
      const next = storedRecord(s, type, current, link);
      if (base.parentId) next.parentId = base.parentId; else delete next.parentId;
      moved.set(profileKey(child), next);
    }
  }
  const archived = keys.map((key) => archiveProfile(s, type, key, operationId));
  await commit([...[...moved.values()].filter((p) => !keys.includes(profileKey(p))), ...archived], operationId);
}

export async function resetSettingToBase(type: PresetType, key: string, setting: string) {
  const s = getState(), p = record(s, type, key);
  await commit([storedRecord(s, type, p, resetToBase(linkOf(p, type), setting))]);
}
export async function detachFromBase(type: PresetType, key: string) {
  const s = getState(), p = record(s, type, key), links = profileChain(s, type, resolveProfile(s, type, key));
  let link = linkOf(p, type);
  for (const ancestor of links.slice(0, -1).reverse()) link = foldBase(ancestor, link);
  const next = storedRecord(s, type, p, link);
  delete next.parentId;
  await commit([next]);
}

export type { SettingOrigin };
export type SettingSource = SettingOrigin;
const sourceCache = new WeakMap<State, Map<string, Record<string, SettingOrigin>>>();
export function settingSources(s: State, type: PresetType, key: string): Record<string, SettingOrigin> {
  let byProfile = sourceCache.get(s);
  if (!byProfile) sourceCache.set(s, (byProfile = new Map()));
  const id = `${type}:${key}`, hit = byProfile.get(id);
  if (hit) return hit;
  const out = type === 'process' ? originsOf(profileChain(s, type, resolveProfile(s, type, key))) : {};
  byProfile.set(id, out);
  return out;
}
