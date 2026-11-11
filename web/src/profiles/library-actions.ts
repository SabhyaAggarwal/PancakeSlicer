import type { PresetType, UserPreset } from '../api/types';
import { findProfile, profileKey, withProfileIdentity } from './identity';
import { catalogAnchor, findCatalogBase, resolveProfile, type ProfileCatalog } from './model';
export const visibleProfiles = (s: Pick<ProfileCatalog,'userPresets'>, type: PresetType, kind: 'profile' | 'master' = 'profile') =>
  s.userPresets.filter(p => p.type === type && (p.kind ?? 'profile') === kind && !p.archived);
export function archiveProfile(s: ProfileCatalog, type: PresetType, key: string, operationId: string): UserPreset {
  const resolved = resolveProfile(s,type,key), source = resolved.source;
  if (!source) throw new Error('That profile is out of sync—refresh your library before removing it.');
  if (source.archived) return source;
  const anchor = source.base ?? (resolved.base && catalogAnchor(resolved.base));
  return {...withProfileIdentity(source), revision:(source.revision ?? 0)+1, archived:{at:Date.now(),operationId},
    snapshot:anchor ? {base:anchor,config:structuredClone(resolved.config)} : source.snapshot};
}
export function restoreArchivedProfile(profile: UserPreset): UserPreset {
  if (!profile.archived) return profile;
  const {archived:_archived,...rest} = withProfileIdentity(profile);
  return {...rest,revision:(profile.revision ?? 0)+1};
}
export function editableMaster(s: ProfileCatalog, type: PresetType, baseKey: string): UserPreset {
  const existing = findProfile(s,type,baseKey);
  if (existing?.kind === 'master' && profileKey(existing) === baseKey) return existing;
  const found = findCatalogBase(s,type,baseKey), base = found.base;
  if (!base) throw new Error(found.ambiguous ? 'Please select the brand for the base template.' : 'Base template unavailable—try refreshing the catalog.');
  const masters = s.userPresets.filter(p => p.type === type && p.kind === 'master' && p.base && findCatalogBase(s,type,p.base).base === base);
  if (masters.length > 1) throw new Error('Multiple custom profiles share this base template. Please pick the exact one you want to use.');
  if (masters[0]) return masters[0];
  return {id:`profile:${crypto.randomUUID()}`,name:base.name,type,kind:'master',revision:0,base:catalogAnchor(base),inherits:base.name,
    overlay:{},overrides:{},baseline:{},snapshot:{base:catalogAnchor(base),config:structuredClone(base.config)},
    provenance:{source:'catalog',sourceName:base.name,operationId:`master:${type}:${base.vendor}:${base.name}`}};
}
export function linkedProfiles(s: ProfileCatalog, type: PresetType, masterKey: string): UserPreset[] {
  const master = findProfile(s,type,masterKey); if (!master) return [];
  const base = master.base && findCatalogBase(s,type,master.base).base;
  return s.userPresets.filter(p => p.type === type && profileKey(p) !== profileKey(master) &&
    (p.masterId === profileKey(master) || !p.masterId && p.kind !== 'master' && !!base && !!p.base && findCatalogBase(s,type,p.base).base === base));
}
