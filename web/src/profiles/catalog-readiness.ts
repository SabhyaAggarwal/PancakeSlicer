import type { PresetType } from '../api/types';
import type { ProfileBaseAnchor } from './ownership';

export interface CatalogBaseDependency { type: PresetType; base: ProfileBaseAnchor }
export interface CatalogDependencySource {
  complete: boolean;
  bases: readonly CatalogBaseDependency[];
}
export interface CatalogReadinessPlan {
  required: string[];
  background: string[];
  missing: CatalogBaseDependency[];
  incomplete: boolean;
}

export function planCatalogReadiness(vendors: readonly string[], sources: readonly CatalogDependencySource[],
  available: (dependency: CatalogBaseDependency) => boolean): CatalogReadinessPlan {
  const ordered = [...new Set(vendors)], needed = new Set<string>(), seen = new Set<string>();
  const missing: CatalogBaseDependency[] = [];
  const incomplete = sources.some(source => !source.complete);
  let unknown = incomplete;
  for (const source of sources) for (const dependency of source.bases) {
    const { type, base } = dependency;
    const key = JSON.stringify([type, base.name, base.vendor, base.id, base.engineName, base.version]);
    if (seen.has(key)) continue;
    seen.add(key);
    if (available(dependency)) continue;
    missing.push(dependency);
    if (base.vendor && ordered.includes(base.vendor)) needed.add(base.vendor);
    else unknown = true;
  }
  return { required: ordered.filter(vendor => unknown || needed.has(vendor)),
    background: ordered.filter(vendor => !unknown && !needed.has(vendor)), missing, incomplete };
}

export async function loadRequiredCatalogs(plan: CatalogReadinessPlan, current: () => boolean,
  load: (vendor: string) => Promise<void>): Promise<boolean> {
  for (const vendor of plan.required) {
    if (!current()) return false;
    await load(vendor);
  }
  return current();
}

export async function loadBackgroundCatalogs(plan: CatalogReadinessPlan, ports: {
  current(): boolean;
  yield(): Promise<void>;
  load(vendor: string): Promise<void>;
  failed(vendor: string, error: unknown): void;
}): Promise<void> {
  for (const vendor of plan.background) {
    if (!ports.current()) return;
    await ports.yield();
    if (!ports.current()) return;
    try { await ports.load(vendor); }
    catch (error) { if (ports.current()) ports.failed(vendor, error); }
  }
}
