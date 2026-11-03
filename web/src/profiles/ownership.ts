import type { Config, PresetType } from '../api/types';
import type { ProfileKey } from './identity';

export interface ProfileBaseAnchor {
  name: string;
  version: string;
  vendor?: string;
  id?: string;
  engineName?: string;
}
export interface ProfileProvenance {
  source: 'catalog' | 'profile' | 'import' | 'migration';
  sourceId?: string;
  sourceName?: string;
  sourceRevision?: number;
  operationId: string;
}
export interface ProfileConfigSnapshot {
  config: Config;
  base: ProfileBaseAnchor;
}
export interface ProfileOwnership {
  id?: ProfileKey;
  applicability?: import('./applicability').ProfileApplicability;
  kind?: 'profile' | 'master';
  masterId?: ProfileKey;
  archived?: { at: number; operationId: string };
  printerId?: ProfileKey;
  revision?: number;
  provenance?: ProfileProvenance;
  snapshot?: ProfileConfigSnapshot;
}
export interface PrinterMaterialSlot {
  id: string;
  materialId: ProfileKey;
  colour?: string;
  overrides?: Config;
  metadata?: Record<string, unknown>;
  snapshot?: ProjectProfileSnapshot;
}
export interface PrinterMaterialSetup {
  schema: 1;
  printerId: ProfileKey;
  revision: number;
  materialIds: ProfileKey[];
  processId?: ProfileKey;
  slots: PrinterMaterialSlot[];
  assignments: Record<string, string>;
  tuning: Record<ProfileKey, Config>;
  mixes: Record<number, { components: number[]; ratios: number[]; gradient: 0 | 1 | null; perPart: boolean }>;
}
export interface PrinterProfileLibrary {
  schema: 1;
  revision: number;
  setups: Record<ProfileKey, PrinterMaterialSetup>;
}
export interface ProjectProfileSnapshot extends ProfileConfigSnapshot {
  id: ProfileKey;
  type: PresetType;
  name: string;
  printerId?: ProfileKey;
  revision: number;
  profileConfig?: Config;
  profile?: import('../api/types').ProfileOverlay;
}
export interface ProjectProfileReferences {
  schema: 1;
  printer: ProjectProfileSnapshot;
  process: ProjectProfileSnapshot;
  filaments: ProjectProfileSnapshot[];
  slots?: PrinterMaterialSlot[];
}
export type ProfileRecoveryReason = 'missing-profile' | 'missing-base' | 'missing-owner' | 'ambiguous' | 'cycle' | 'foreign-owner' | 'archived' | 'incompatible';
export interface ProfileRecovery {
  reason: ProfileRecoveryReason;
  key: ProfileKey;
  type: PresetType;
  snapshot?: ProfileConfigSnapshot;
}
export interface ProfileLibraryDocument {
  schema: 1;
  revision: number;
  epoch?: string;
  profiles: import('../api/types').ProfileOverlay[];
  catalog?: import('../api/types').SystemPreset[];
  printers: PrinterProfileLibrary;
}
export interface ProfileLibraryWrite {
  operationId: string;
  expectedRevision: number;
  document: ProfileLibraryDocument;
  inventory?: {
    complete: true;
    libraryRevision: number;
    projectRevision?: string;
    references: Record<ProfileKey, readonly import('./identity').ProfileReference[]>;
  };
}
export interface ProfileLibraryWriteResult { revision: number; operationId: string }

export const emptyPrinterLibrary = (): PrinterProfileLibrary => ({ schema: 1, revision: 0, setups: {} });
export const emptyPrinterSetup = (printerId: ProfileKey): PrinterMaterialSetup => ({
  schema: 1, printerId, revision: 0, materialIds: [], slots: [], assignments: {}, tuning: {}, mixes: {},
});
