import type { PresetType } from '../api/types';
import type { ProfileBaseAnchor } from './ownership';

export interface CatalogBaseDependency { type: PresetType; base: ProfileBaseAnchor }
export interface CatalogDependencySource {
  complete: boolean;
  bases: readonly CatalogBaseDependency[];
}
