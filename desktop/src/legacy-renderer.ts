import { createHash } from 'node:crypto';
import path from 'node:path';

export const DESKTOP_ORIGIN = 'http://127.0.0.1:29731' as const;
export type RendererCoverage = 'decoded' | 'verified-absent' | 'incomplete';
export interface RendererDomainCoverage {
  localStorage: RendererCoverage;
  indexedDB: RendererCoverage;
  sessionStorage: RendererCoverage;
  blobs: RendererCoverage;
}
export type RendererBinaryKind = 'ArrayBuffer' | 'Blob' | 'File' | 'Int8Array' | 'Uint8Array'
  | 'Uint8ClampedArray' | 'Int16Array' | 'Uint16Array' | 'Int32Array' | 'Uint32Array'
  | 'Float32Array' | 'Float64Array' | 'BigInt64Array' | 'BigUint64Array' | 'DataView';
export type RendererPartReference =
  | { $orcaRendererPart: string; kind: Exclude<RendererBinaryKind, 'Blob' | 'File'> }
  | { $orcaRendererPart: string; kind: 'Blob'; mimeType: string }
  | { $orcaRendererPart: string; kind: 'File'; mimeType: string; name: string; lastModified: number };
export type RendererDecodedValue = null | boolean | number | string | RendererPartReference
  | { $orcaDate: number } | RendererDecodedValue[] | { [key: string]: RendererDecodedValue };
export interface RendererDatabase {
  name: string;
  version: number;
  stores: { name: string; records: { key: RendererDecodedValue; value: RendererDecodedValue }[]; metadata?: Record<string, RendererDecodedValue> }[];
  metadata?: Record<string, RendererDecodedValue>;
}
export interface RendererOriginInventory {
  origin: string;
  coverage: RendererDomainCoverage;
  localStorage: { key: string; value: string }[];
  databases: RendererDatabase[];
  sessionStorageNamespaces: { namespaceId: string; values: { key: string; value: string }[] }[];
  binaryParts: { key: string; size: number; sha256: string }[];
}
export interface RendererInventoryIssue {
  code: 'decoder_unavailable' | 'unsupported_layout' | 'unverified_absence' | 'source_locked'
    | 'source_changed' | 'unsupported_type' | 'missing_part' | 'sensitive_source' | 'invalid_origin';
  domain: 'profile' | keyof RendererDomainCoverage;
  sourceRef: string;
  message: string;
}
export interface LegacyRendererInventory {
  version: 1;
  status: 'COMPLETE' | 'INCOMPLETE';
  rendererOrigin: typeof DESKTOP_ORIGIN;
  sourceProfileId: string;
  inventoryFingerprint: string;
  coverage: RendererDomainCoverage;
  absence: { supportedLayout: string; sourceRefs: string[] } | null;
  origins: RendererOriginInventory[];
  issues: RendererInventoryIssue[];
}
export interface LegacyRendererPartRead {
  inventoryFingerprint: string;
  key: string;
  offset: number;
  length: number;
}

export class LegacyRendererSource {
  private readonly originalRoots: Readonly<{ userData: string; sessionData: string }>;
  private readonly sourceProfileId: string;

  constructor(userData: string, sessionData: string) {
    this.originalRoots = Object.freeze({ userData: path.resolve(userData), sessionData: path.resolve(sessionData) });
    this.sourceProfileId = `electron-session-data:v1:${createHash('sha256').update(JSON.stringify(this.originalRoots)).digest('hex')}`;
  }

  async inventory(): Promise<LegacyRendererInventory> {
    const body = {
      version: 1 as const, status: 'INCOMPLETE' as const, rendererOrigin: DESKTOP_ORIGIN,
      sourceProfileId: this.sourceProfileId,
      coverage: { localStorage: 'incomplete', indexedDB: 'incomplete', sessionStorage: 'incomplete', blobs: 'incomplete' } as RendererDomainCoverage,
      absence: null, origins: [] as RendererOriginInventory[],
      issues: [{ code: 'decoder_unavailable', domain: 'profile', sourceRef: 'original-chromium-session-data',
        message: 'Read-only historical renderer storage decoding is unavailable. Original files are retained; historical absence and adoption are not established.' }] as RendererInventoryIssue[],
    };
    return { ...body, inventoryFingerprint: createHash('sha256').update(JSON.stringify(body)).digest('hex') };
  }

  async readPart(_request: LegacyRendererPartRead): Promise<Uint8Array> {
    throw new Error('Historical renderer inventory is incomplete; no binary part is approved for export.');
  }
}
