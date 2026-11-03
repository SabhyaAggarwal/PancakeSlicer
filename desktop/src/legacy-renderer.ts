import { createHash } from 'node:crypto';
import path from 'node:path';
import { isMainThread, parentPort, Worker, workerData } from 'node:worker_threads';

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
  private scanning: Promise<LegacyRendererInventory> | null = null;
  private physicalFingerprint: string | null = null;
  private changed = false;
  private serial: Promise<void> = Promise.resolve();
  private latestInventory: string | null = null;
  private approved = new Map<string, { origin: string; size: number; sha256: string }>();

  constructor(userData: string, sessionData: string, private readonly ownsOriginalSingleton: () => boolean = () => false) {
    this.originalRoots = Object.freeze({ userData: path.resolve(userData), sessionData: path.resolve(sessionData) });
    this.sourceProfileId = `electron-session-data:v1:${createHash('sha256').update(JSON.stringify(this.originalRoots)).digest('hex')}`;
  }

  inventory(): Promise<LegacyRendererInventory> {
    if (this.scanning) return this.scanning;
    if (this.changed) return Promise.resolve(incomplete(this.sourceProfileId, 'source_changed', 'Historical source changed or could not be reverified. Restart explicit recovery; original files are retained.'));
    if (this.originalRoots.userData !== this.originalRoots.sessionData || !this.ownsOriginalSingleton())
      return Promise.resolve(incomplete(this.sourceProfileId, 'source_locked', 'Same-root legacy layout and original-userData singleton exclusion are not established.'));
    this.scanning = this.enqueue(async () => (await this.scan()).inventory).finally(() => { this.scanning = null; });
    return this.scanning;
  }

  private enqueue<T>(work: () => Promise<T>): Promise<T> {
    const next = this.serial.then(work); this.serial = next.then(() => undefined, () => undefined); return next;
  }
  private async scan(part?: PrivatePartRead): Promise<WorkerResult> {
    if (this.changed || !this.ownsOriginalSingleton()) return { inventory: incomplete(this.sourceProfileId, 'source_locked', 'Original-source exclusion or freshness is not established.') };
    const result = await new Promise<WorkerResult>((resolve) => {
      let worker: Worker;
      try {
        worker = new Worker(__filename, { workerData: { kind: 'orca-legacy-renderer-v1',
          root: this.originalRoots.sessionData, sourceProfileId: this.sourceProfileId,
          ...(part ? { part, expectedPhysical: this.physicalFingerprint } : {}) },
          resourceLimits: { maxOldGenerationSizeMb: 128, maxYoungGenerationSizeMb: 16, stackSizeMb: 4 } });
      } catch {
        resolve({ inventory: incomplete(this.sourceProfileId, 'unsupported_layout', 'Read-only decoder worker is unavailable.') }); return;
      }
      let finished = false;
      const finish = async (r: WorkerResult) => {
        if (finished) return; finished = true; clearTimeout(timer);
        try { await worker.terminate(); } catch {  }
        resolve(r);
      };
      const failed = () => ({ inventory: incomplete(this.sourceProfileId, 'unsupported_layout', 'Read-only decoding failed or exceeded its bounded budget; original files are retained.') });
      const timer = setTimeout(() => void finish(failed()), 30_000);
      worker.once('message', (r: WorkerResult) => void finish(r));
      worker.once('error', () => void finish(failed()));
      worker.once('exit', () => void finish(failed()));
    });
    if (!this.ownsOriginalSingleton()) { this.approved.clear(); return { inventory: incomplete(this.sourceProfileId, 'source_locked', 'Original-userData singleton exclusion was lost.') }; }
    if (result.inventory.status === 'COMPLETE') {
      if (!result.physicalFingerprint || this.physicalFingerprint !== null && this.physicalFingerprint !== result.physicalFingerprint) {
        this.changed = true;
        this.approved.clear();
        return { inventory: incomplete(this.sourceProfileId, 'source_changed', 'Historical source changed between inventories; no adoption is established.') };
      }
      this.physicalFingerprint = result.physicalFingerprint;
      this.latestInventory = result.inventory.inventoryFingerprint;
      this.approved = new Map(result.inventory.origins.flatMap((origin) => origin.binaryParts.map((p) => [p.key, { origin: origin.origin, size: p.size, sha256: p.sha256 }] as const)));
    } else { this.approved.clear(); this.latestInventory = null; if (this.physicalFingerprint !== null) this.changed = true; }
    return result;
  }

  readPart(request: LegacyRendererPartRead): Promise<Uint8Array> {
    return this.enqueue(async () => {
      const descriptor = request && this.approved.get(request.key);
      if (!descriptor || this.changed || !this.physicalFingerprint || request.inventoryFingerprint !== this.latestInventory
        || !Number.isSafeInteger(request.offset) || !Number.isSafeInteger(request.length) || request.offset < 0 || request.length < 0
        || request.length > 4 * 1024 * 1024 || request.offset > descriptor.size || request.length > descriptor.size - request.offset)
        throw new Error('Historical binary descriptor or range is not approved.');
      const result = await this.scan({ ...request, ...descriptor });
      if (result.inventory.status !== 'COMPLETE' || result.inventory.inventoryFingerprint !== request.inventoryFingerprint
        || !(result.chunk instanceof Uint8Array) || result.chunk.length !== request.length)
        throw new Error('Historical binary source could not be reverified completely.');
      return result.chunk;
    });
  }
}

type InventoryBody = Omit<LegacyRendererInventory, 'inventoryFingerprint'>;
interface PrivatePartRead extends LegacyRendererPartRead { origin: string; size: number; sha256: string }
interface WorkerResult { inventory: LegacyRendererInventory; physicalFingerprint?: string; chunk?: Uint8Array }
function fingerprint(body: InventoryBody): LegacyRendererInventory {
  return { ...body, inventoryFingerprint: createHash('sha256').update(JSON.stringify(body)).digest('hex') };
}
function incomplete(sourceProfileId: string, code: RendererInventoryIssue['code'], message: string): LegacyRendererInventory {
  return fingerprint({ version: 1, status: 'INCOMPLETE', rendererOrigin: DESKTOP_ORIGIN, sourceProfileId,
    coverage: { localStorage: 'incomplete', indexedDB: 'incomplete', sessionStorage: 'incomplete', blobs: 'incomplete' },
    absence: null, origins: [], issues: [{ code, domain: 'profile', sourceRef: 'original-chromium-session-data', message }] });
}

if (!isMainThread && workerData?.kind === 'orca-legacy-renderer-v1') {
  void (async () => {
    const { scanOriginal } = await import('./legacy-renderer/storage');
    const { Incomplete } = await import('./legacy-renderer/leveldb');
    const id = workerData.sourceProfileId as string;
    let result: WorkerResult;
    try {
      const decoded = await scanOriginal(workerData.root as string);
      const inventory = fingerprint({ version: 1, status: 'COMPLETE', rendererOrigin: DESKTOP_ORIGIN, sourceProfileId: id,
        coverage: decoded.coverage, absence: decoded.absence, origins: decoded.origins, issues: [] });
      result = { inventory, physicalFingerprint: decoded.physicalFingerprint };
      const part = workerData.part as PrivatePartRead | undefined;
      if (part) {
        const approved = decoded.parts.get(part.key);
        if (!approved || inventory.inventoryFingerprint !== part.inventoryFingerprint || decoded.physicalFingerprint !== workerData.expectedPhysical
          || approved.origin !== part.origin || approved.sha256 !== part.sha256 || approved.size !== part.size
          || !Number.isSafeInteger(part.offset) || !Number.isSafeInteger(part.length) || part.offset < 0 || part.length < 0
          || part.length > 4 * 1024 * 1024 || part.offset > approved.size || part.length > approved.size - part.offset)
          throw new Incomplete('source_changed', 'Approved binary ownership, hash or physical snapshot changed.');
        result.chunk = new Uint8Array(approved.bytes.subarray(part.offset, part.offset + part.length));
      }
    } catch (error) {
      result = { inventory: error instanceof Incomplete ? incomplete(id, error.code, error.message)
        : incomplete(id, 'unsupported_layout', 'Original storage could not be read completely in a supported layout.') };
    }
    parentPort?.postMessage(result);
  })().catch(() => { parentPort?.close(); });
}
