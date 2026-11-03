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

export function findProfile(s: Pick<State, 'userPresets'>, type: PresetType, key: ProfileKey): UserPreset | undefined {
  const profiles = s.userPresets.filter((p) => p.type === type);
  const exact = profiles.filter((p) => profileKey(p) === key);
  if (exact.length === 1) return exact[0];
  const named = profiles.filter((p) => p.name === key);
  if (named.length) return named.length === 1 ? named[0] : undefined;
  const aliases = profiles.filter((p) => p.aliases?.includes(key));
  return aliases.length === 1 ? aliases[0] : undefined;
}

export function profileReferenceInventory(s: State, type: PresetType, key: ProfileKey, external?: ProfileReferenceSource): ProfileReferenceInventory {
  const p = findProfile(s, type, key);
  if (!p) return { complete: false, references: [] };
  const id = profileKey(p), references: ProfileReference[] = [];
  const selected = type === 'machine' ? [s.printer] : type === 'process' ? [s.process] : s.filamentSlots;
  selected.forEach((selectedKey, slot) => {
    if (!matchesProfile(p, selectedKey)) return;
    references.push({ kind: 'active', label: type === 'filament' ? `Filament ${slot + 1}` : type === 'machine' ? 'Printer' : 'Process' });
    if (s.project || s.projectName) references.push({ kind: 'project', label: s.projectName ?? s.project?.name ?? 'Current project' });
  });
  for (const child of s.userPresets) if (child.type === type && (child.parentId === id || child.parent && matchesProfile(p, child.parent)))
    references.push({ kind: 'inheritance', label: child.name, owner: profileKey(child) });
  for (const item of external?.references ?? []) if (item.type === type && matchesProfile(p, item.key)) references.push(item.reference);
  return { complete: external?.complete === true, references };
}
export const profileReferences = (s: State, type: PresetType, key: ProfileKey, source?: ProfileReferenceSource) => profileReferenceInventory(s, type, key, source).references;
export const isProfileDeletable = (s: State, type: PresetType, key: ProfileKey, source?: ProfileReferenceSource) => {
  const inventory = profileReferenceInventory(s, type, key, source);
  return inventory.complete && inventory.references.length === 0;
};
