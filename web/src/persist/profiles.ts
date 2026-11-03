import type { ProfileOverlay, UserPreset, PresetType, SystemPreset } from '../api/types';
import { normalizeUserPreset, storedProfile } from '../profiles/model';
import { sameValue } from '../state/value-equality';

const USER_KEY = 'orca.userPresets', OVERLAY_KEY = 'orca.profileOverlays.v1';
interface Entry { profile: ProfileOverlay; localOnly?: boolean; wire: Pick<UserPreset, 'inherits' | 'overrides'> }
const memory = new Map<string, unknown[]>();
function read<T>(key: string): T[] {
  if (memory.has(key)) return memory.get(key) as T[];
  try { const value = JSON.parse(localStorage.getItem(key) ?? '[]'); return Array.isArray(value) ? value : []; }
  catch { return []; }
}
function write(key: string, value: unknown[]) {
  try { localStorage.setItem(key, JSON.stringify(value)); memory.delete(key); }
  catch { memory.set(key, value); }
}
const identity = (p: { type: PresetType; name: string }, type: PresetType, name: string) => p.type === type && p.name === name;

export const localProfiles = (): UserPreset[] => read<UserPreset | ProfileOverlay>(USER_KEY).map(normalizeUserPreset);
export function rememberProfile(p: UserPreset, wire: Pick<UserPreset, 'inherits' | 'overrides'> = p, mock = false, migration = false) {
  if (migration && !mock) {
    const saved = read<Entry>(OVERLAY_KEY).find((e) => identity(e.profile, p.type, p.name));
    const owned = p.overlay ?? p.overrides;
    if (saved && saved.profile.base.name === p.base?.name && saved.profile.parent === p.parent
      && [...new Set([...Object.keys(saved.profile.overlay), ...Object.keys(owned)])].every((key) => sameValue(key, saved.profile.overlay[key], owned[key]))) return;
  }
  if (mock) write(USER_KEY, [...read<UserPreset | ProfileOverlay>(USER_KEY).filter((u) => !identity(u, p.type, p.name)), storedProfile(p)]);
  else write(OVERLAY_KEY, [...read<Entry>(OVERLAY_KEY).filter((e) => !identity(e.profile, p.type, p.name)), { profile: storedProfile(p), wire }]);
}
export function forgetProfile(type: PresetType, name: string, mock = false) {
  if (mock) write(USER_KEY, read<UserPreset | ProfileOverlay>(USER_KEY).filter((u) => !identity(u, type, name)));
  else write(OVERLAY_KEY, read<Entry>(OVERLAY_KEY).filter((e) => !identity(e.profile, type, name)));
}

export function restoreProfiles(remote: UserPreset[]): UserPreset[] {
  const entries = read<Entry>(OVERLAY_KEY);
  const restored = remote.map((raw) => {
    const p = normalizeUserPreset(raw);
    const saved = entries.find((e) => identity(e.profile, p.type, p.name));
    if (!saved || saved.wire.inherits !== p.inherits) return p;
    const keys = new Set([...Object.keys(saved.wire.overrides), ...Object.keys(p.overrides)]);
    if ([...keys].some((key) => !sameValue(key, saved.wire.overrides[key], p.overrides[key]))) return p;
    return normalizeUserPreset(saved.profile);
  });
  return [...restored, ...entries.filter((e) => e.localOnly && !remote.some((p) => identity(e.profile, p.type, p.name))).map((e) => normalizeUserPreset(e.profile))];
}

export function rememberLocalProfile(p: UserPreset, mock = false) {
  rememberProfile(p, p, mock);
  if (!mock) write(OVERLAY_KEY, read<Entry>(OVERLAY_KEY).map((e) => identity(e.profile, p.type, p.name) ? { ...e, localOnly: true } : e));
}
export const defaultsInitialized = () => read<string>('orca.profileDefaults.v1').includes('ready');
export const markDefaultsInitialized = () => write('orca.profileDefaults.v1', ['ready']);

export const importedBases = () => read<SystemPreset>('orca.profileBases.v1');
export function rememberBase(base: SystemPreset) {
  write('orca.profileBases.v1', [...importedBases().filter((p) => !identity(p, base.type, base.name)), base]);
}

export function rememberProfileUpdate(p: UserPreset, mock = false) {
  if (mock) return rememberProfile(p, p, true);
  const entries = read<Entry>(OVERLAY_KEY);
  if (!entries.some((e) => identity(e.profile, p.type, p.name))) return rememberLocalProfile(p);
  write(OVERLAY_KEY, entries.map((e) => identity(e.profile, p.type, p.name) ? { ...e, profile: storedProfile(p) } : e));
}
