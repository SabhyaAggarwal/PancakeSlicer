import type { PresetType, UserPreset } from '../api/types';
import type { State } from '../state/store';

export type ProfileKey = string;
export interface ProfileReference {
  kind: 'active' | 'default' | 'project' | 'inheritance';
  label: string;
  owner?: ProfileKey;
}
export interface ProfileReferenceSource {
  complete: boolean;
  references: readonly { type: PresetType; key: ProfileKey; reference: ProfileReference }[];
}
export interface ProfileReferenceInventory { complete: boolean; references: readonly ProfileReference[] }

const assigned = new WeakMap<UserPreset, ProfileKey>();
export function profileKey(profile: UserPreset): ProfileKey {
  if (profile.id) return profile.id;
  let id = assigned.get(profile);
  if (!id) { id = `profile:${crypto.randomUUID()}`; assigned.set(profile, id); }
  return id;
}
export const withProfileIdentity = (profile: UserPreset): UserPreset => profile.id ? profile : { ...profile, id: profileKey(profile) };
export const matchesProfile = (profile: UserPreset, key: ProfileKey) => profileKey(profile) === key || profile.name === key || !!profile.aliases?.includes(key);

interface ProfileIndex { ids: Map<string, UserPreset | null>; names: Map<string, UserPreset | null>; aliases: Map<string, UserPreset | null> }
const profileIndexes = new WeakMap<UserPreset[], Map<string, ProfileIndex>>();
const indexProfile = (map: Map<string, UserPreset | null>, key: string, profile: UserPreset) => {
  if (!map.has(key)) map.set(key, profile); else map.set(key, null);
};
export function lookupProfile(s: Pick<State, 'userPresets'>, type: PresetType, key: ProfileKey, printerId?: ProfileKey): { profile?: UserPreset; ambiguous: boolean } {
  let scopes = profileIndexes.get(s.userPresets);
  if (!scopes) profileIndexes.set(s.userPresets, (scopes = new Map()));
  const scope = JSON.stringify([type, printerId]);
  let index = scopes.get(scope);
  if (!index) {
    index = { ids: new Map(), names: new Map(), aliases: new Map() };
    for (const profile of s.userPresets) if (profile.type === type && (printerId === undefined || profile.printerId === printerId)) {
      indexProfile(index.ids, profileKey(profile), profile);
      indexProfile(index.names, profile.name, profile);
      for (const alias of new Set(profile.aliases ?? [])) indexProfile(index.aliases, alias, profile);
    }
    scopes.set(scope, index);
  }
  for (const map of [index.ids, index.names, index.aliases]) if (map.has(key)) {
    const profile = map.get(key);
    return { profile: profile ?? undefined, ambiguous: profile === null };
  }
  return { ambiguous: false };
}
export const findProfile = (s: Pick<State, 'userPresets'>, type: PresetType, key: ProfileKey, printerId?: ProfileKey): UserPreset | undefined => lookupProfile(s, type, key, printerId).profile;

export function profileReferenceInventory(s: State, type: PresetType, key: ProfileKey, external?: ProfileReferenceSource): ProfileReferenceInventory {
  const p = findProfile(s, type, key);
  if (!p) return { complete: false, references: [] };
  const references: ProfileReference[] = [];
  const selected = type === 'machine' ? [s.printer] : type === 'process' ? [s.process] : s.filamentSlots;
  selected.forEach((selectedKey, slot) => {
    if (!matchesProfile(p, selectedKey)) return;
    references.push({ kind: 'active', label: type === 'filament' ? `Filament ${slot + 1}` : type === 'machine' ? 'Printer' : 'Process' });
    if (s.project || s.projectName) references.push({ kind: 'project', label: s.projectName ?? s.project?.name ?? 'Current project' });
  });
  for (const child of s.userPresets) if (child.type === type && child.parent && matchesProfile(p, child.parent))
    references.push({ kind: 'inheritance', label: child.name, owner: profileKey(child) });
  for (const item of external?.references ?? []) if (item.type === type && matchesProfile(p, item.key)) references.push(item.reference);
  return { complete: external?.complete === true, references };
}
export const profileReferences = (s: State, type: PresetType, key: ProfileKey, source?: ProfileReferenceSource) => profileReferenceInventory(s, type, key, source).references;
export const isProfileDeletable = (s: State, type: PresetType, key: ProfileKey, source?: ProfileReferenceSource) => {
  const inventory = profileReferenceInventory(s, type, key, source);
  return inventory.complete && inventory.references.length === 0;
};
