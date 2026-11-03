import type { LegacyDatabase } from './legacy-projects';
import type { LegacyRendererInventory, LegacyRendererPartRead, RendererOriginInventory, RendererDomainCoverage,
  RendererPartReference } from '../../../desktop/src/legacy-renderer';
import { bytesBase64, parseDocument, sha256 } from './wire';
export type { LegacyRendererInventory, RendererOriginInventory } from '../../../desktop/src/legacy-renderer';

export interface LegacyDesktopRenderer {
  version: 1; inventory(): Promise<LegacyRendererInventory>;
  readPart(request: LegacyRendererPartRead): Promise<Uint8Array>;
}
export interface CapturedRendererOrigin extends Omit<RendererOriginInventory, 'databases'> { databases: LegacyDatabase[] }
export interface DesktopRendererCapture {
  inventory: LegacyRendererInventory;
  origins: CapturedRendererOrigin[];
  parts: { key: string; blob: Blob }[];
}
export const desktopLegacyIncomplete = () => Object.assign(new Error('Historical desktop renderer storage is incomplete. Original sources are retained; normal writes remain blocked.'),
  { name: 'DesktopLegacyIncompleteError', code: 'legacy_renderer_incomplete' });
const rendererIncomplete = desktopLegacyIncomplete;
const domains = ['localStorage', 'indexedDB', 'sessionStorage', 'blobs'] as const;
const completeCoverage = (coverage: RendererDomainCoverage) => coverage && domains.every((key) => coverage[key] === 'decoded' || coverage[key] === 'verified-absent');
const digestPattern = /^[a-f0-9]{64}$/;
function jsonTransport(value: unknown, seen = new Set<object>()): boolean {
  if (value === null || typeof value === 'string' || typeof value === 'boolean') return true;
  if (typeof value === 'number') return Number.isFinite(value) && !Object.is(value, -0);
  if (!value || typeof value !== 'object' || seen.has(value)) return false;
  if (!Array.isArray(value) && Object.getPrototypeOf(value) !== Object.prototype && Object.getPrototypeOf(value) !== null) return false;
  seen.add(value);
  return Reflect.ownKeys(value).length === Object.keys(value).length + (Array.isArray(value) ? 1 : 0)
    && (!Array.isArray(value) || Object.keys(value).length === value.length && Object.keys(value).every((key, index) => key === String(index)))
    && Object.values(value).every((item) => jsonTransport(item, seen));
}
async function checkRendererInventory(inventory: LegacyRendererInventory): Promise<void> {
  if (!jsonTransport(inventory) || !inventory || inventory.version !== 1 || inventory.status !== 'COMPLETE'
    || inventory.rendererOrigin !== 'http://127.0.0.1:29731' || !/^electron-session-data:v1:[a-f0-9]{64}$/.test(inventory.sourceProfileId)
    || !digestPattern.test(inventory.inventoryFingerprint) || !completeCoverage(inventory.coverage)
    || !Array.isArray(inventory.origins) || !Array.isArray(inventory.issues) || inventory.issues.length) throw rendererIncomplete();
  if (Object.keys(inventory).join(',') !== 'version,status,rendererOrigin,sourceProfileId,coverage,absence,origins,issues,inventoryFingerprint'
    || Object.keys(inventory.coverage).join(',') !== domains.join(',')) throw rendererIncomplete();
  if ((!inventory.origins.length || domains.some((key) => inventory.coverage[key] === 'verified-absent')) && (!inventory.absence || typeof inventory.absence.supportedLayout !== 'string' || !inventory.absence.supportedLayout
    || !Array.isArray(inventory.absence.sourceRefs) || !inventory.absence.sourceRefs.length
    || inventory.absence.sourceRefs.some((ref) => typeof ref !== 'string' || !ref || /^[\\/]|^[A-Za-z]:/.test(ref) || ref.split('/').some((part) => part === '..'))
    || !inventory.origins.length && domains.some((key) => inventory.coverage[key] !== 'verified-absent'))) throw rendererIncomplete();
  const origins = new Set<string>();
  for (const origin of inventory.origins) {
    let url: URL;
    try { url = new URL(origin.origin); } catch { throw rendererIncomplete(); }
    if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.origin !== origin.origin || origins.has(origin.origin)
      || !completeCoverage(origin.coverage) || Object.keys(origin.coverage).join(',') !== domains.join(',')
      || !Array.isArray(origin.localStorage) || !Array.isArray(origin.databases)
      || !Array.isArray(origin.sessionStorageNamespaces) || !Array.isArray(origin.binaryParts)) throw rendererIncomplete();
    origins.add(origin.origin);
    const values = { localStorage: origin.localStorage, indexedDB: origin.databases, sessionStorage: origin.sessionStorageNamespaces, blobs: origin.binaryParts };
    if (domains.some((key) => (origin.coverage[key] === 'verified-absent' && values[key].length)
      || (inventory.coverage[key] === 'verified-absent' && origin.coverage[key] !== 'verified-absent'))) throw rendererIncomplete();
    const unique = (items: readonly string[]) => items.every((key) => typeof key === 'string') && new Set(items).size === items.length;
    const strings = (items: { key: string; value: string }[]) => unique(items.map((item) => item.key)) && items.every((item) => typeof item.value === 'string');
    if (!strings(origin.localStorage) || !unique(origin.databases.map((db) => db.name))
      || !unique(origin.sessionStorageNamespaces.map((ns) => ns.namespaceId))) throw rendererIncomplete();
    for (const ns of origin.sessionStorageNamespaces) if (!ns.namespaceId || !Array.isArray(ns.values) || !strings(ns.values)) throw rendererIncomplete();
    for (const db of origin.databases) {
      if (!db.name || !Number.isSafeInteger(db.version) || db.version < 1 || !Array.isArray(db.stores) || !unique(db.stores.map((store) => store.name))) throw rendererIncomplete();
      for (const store of db.stores) if (!store.name || !Array.isArray(store.records)) throw rendererIncomplete();
    }
  }
  const { inventoryFingerprint, ...body } = inventory;
  if (await sha256(new TextEncoder().encode(JSON.stringify(body))) !== inventoryFingerprint) throw rendererIncomplete();
}
export async function verifyDesktopRendererCapture(source: LegacyDesktopRenderer, capture: DesktopRendererCapture): Promise<void> {
  let current: LegacyRendererInventory;
  try { current = await source.inventory(); } catch { throw rendererIncomplete(); }
  await checkRendererInventory(current);
  if (current.sourceProfileId !== capture.inventory.sourceProfileId || current.inventoryFingerprint !== capture.inventory.inventoryFingerprint) throw rendererIncomplete();
}
export async function captureDesktopRenderer(source: LegacyDesktopRenderer): Promise<DesktopRendererCapture> {
  if (!source || source.version !== 1 || typeof source.inventory !== 'function' || typeof source.readPart !== 'function') throw rendererIncomplete();
  let inventory: LegacyRendererInventory;
  try { inventory = await source.inventory(); } catch { throw rendererIncomplete(); }
  await checkRendererInventory(inventory);
  const parts = new Map<string, Blob>();
  for (const origin of inventory.origins) for (const part of origin.binaryParts) {
    if (typeof part.key !== 'string' || !part.key || parts.has(part.key) || !Number.isSafeInteger(part.size) || part.size < 0 || !digestPattern.test(part.sha256)) throw rendererIncomplete();
    const chunks: BlobPart[] = [];
    for (let offset = 0; offset < part.size;) {
      const length = Math.min(4 * 1024 * 1024, part.size - offset);
      let bytes: Uint8Array;
      try { bytes = await source.readPart({ inventoryFingerprint: inventory.inventoryFingerprint, key: part.key, offset, length }); } catch { throw rendererIncomplete(); }
      if (!(bytes instanceof Uint8Array) || bytes.byteLength !== length) throw rendererIncomplete();
      chunks.push(new Uint8Array(bytes) as BlobPart); offset += length;
    }
    const blob = new Blob(chunks);
    if (await sha256(await blob.arrayBuffer()) !== part.sha256) throw rendererIncomplete();
    parts.set(part.key, blob);
  }
  async function unpack(value: unknown, allowed: Set<string>): Promise<unknown> {
    if (!value || typeof value !== 'object') return value;
    if (Object.hasOwn(value, '$orcaRendererPart')) {
      const ref = value as RendererPartReference, part = parts.get(ref.$orcaRendererPart);
      const fields = ref.kind === 'File' ? ['$orcaRendererPart', 'kind', 'mimeType', 'name', 'lastModified']
        : ref.kind === 'Blob' ? ['$orcaRendererPart', 'kind', 'mimeType'] : ['$orcaRendererPart', 'kind'];
      if (!part || !allowed.has(ref.$orcaRendererPart) || Object.keys(ref).length !== fields.length || Object.keys(ref).some((key) => !fields.includes(key))) throw rendererIncomplete();
      if (ref.kind === 'Blob' || ref.kind === 'File') {
        if (typeof ref.mimeType !== 'string') throw rendererIncomplete();
        const blob = ref.kind === 'Blob' ? new Blob([part], { type: ref.mimeType })
          : typeof ref.name === 'string' && Number.isSafeInteger(ref.lastModified) ? new File([part], ref.name, { type: ref.mimeType, lastModified: ref.lastModified }) : null;
        if (!blob || blob.type !== ref.mimeType || ref.kind === 'File' && (!(blob instanceof File) || blob.name !== ref.name || blob.lastModified !== ref.lastModified)) throw rendererIncomplete();
        return blob;
      }
      try { return parseDocument(JSON.stringify({ $orcaBinary: ref.kind, data: bytesBase64(new Uint8Array(await part.arrayBuffer())) })); }
      catch { throw rendererIncomplete(); }
    }
    if (Object.hasOwn(value, '$orcaDate')) {
      const ms = (value as {$orcaDate: unknown}).$orcaDate;
      if (Object.keys(value).length !== 1 || typeof ms !== 'number' || !Number.isSafeInteger(ms) || !Number.isFinite(new Date(ms).getTime())) throw rendererIncomplete();
      return new Date(ms);
    }
    if (Object.keys(value).some((key) => key.startsWith('$orca'))) throw rendererIncomplete();
    if (Array.isArray(value)) { const result = []; for (const item of value) result.push(await unpack(item, allowed)); return result; }
    const entries: [string, unknown][] = [];
    for (const [key, item] of Object.entries(value)) entries.push([key, await unpack(item, allowed)]);
    return Object.fromEntries(entries);
  }
  const validKey = (key: unknown): key is IDBValidKey => typeof key === 'string' || typeof key === 'number' && Number.isFinite(key)
    || key instanceof Date && Number.isFinite(key.getTime()) || key instanceof ArrayBuffer || ArrayBuffer.isView(key) || Array.isArray(key) && key.every(validKey);
  const origins: CapturedRendererOrigin[] = [];
  for (const origin of inventory.origins) {
    const decoded = await unpack(origin, new Set(origin.binaryParts.map((part) => part.key))) as CapturedRendererOrigin;
    for (const db of decoded.databases) for (const store of db.stores) for (const row of store.records) if (!validKey(row.key)) throw rendererIncomplete();
    origins.push(decoded);
  }
  const capture = { inventory, origins, parts: [...parts].map(([key, blob]) => ({ key, blob })) };
  await verifyDesktopRendererCapture(source, capture);
  return capture;
}
export interface DesktopInventory {
  version: 1; complete: true; source: 'electron-user-data'; fingerprint: string;
  roots: { name: string; paths: string[] }[]; directories: string[];
  files: { path: string; size: number; sha256: string; mode: number; mtimeNs: string }[];
}
export interface LegacyDesktopProjects {
  version: 1; inventory(): Promise<DesktopInventory>;
  readFile(request: { fingerprint: string; path: string; offset: number; length: number }): Promise<Uint8Array>;
}
export interface DesktopCapture { inventory: DesktopInventory; files: { path: string; blob: Blob }[]; database: LegacyDatabase }
export async function captureDesktopProjects(source: LegacyDesktopProjects): Promise<DesktopCapture> {
  if (source.version !== 1) throw new Error('Unsupported desktop legacy inventory capability');
  const inventory = await source.inventory();
  if (inventory.version !== 1 || inventory.complete !== true || inventory.source !== 'electron-user-data' || !Array.isArray(inventory.files) || !inventory.fingerprint)
    throw new Error('Desktop legacy inventory is incomplete. Existing files are retained.');
  const { fingerprint, ...body } = inventory;
  if (await sha256(new TextEncoder().encode(JSON.stringify(body))) !== fingerprint) throw new Error('Invalid desktop inventory fingerprint');
  const files = new Map<string, Blob>(), hashes = new Map<string, string>();
  for (const file of inventory.files) {
    if (!file.path || files.has(file.path) || file.path.split('/').some((part) => !part || part === '.' || part === '..') || file.path.includes('\\') || !Number.isSafeInteger(file.size) || file.size < 0)
      throw new Error('Invalid desktop legacy file descriptor');
    const chunks: BlobPart[] = [];
    for (let offset = 0; offset < file.size;) {
      const length = Math.min(4 * 1024 * 1024, file.size - offset);
      const bytes = await source.readFile({ fingerprint: inventory.fingerprint, path: file.path, offset, length });
      if (!(bytes instanceof Uint8Array) || bytes.byteLength !== length) throw new Error(`Incomplete desktop legacy file: ${file.path}`);
      chunks.push(new Uint8Array(bytes) as BlobPart); offset += length;
    }
    const blob = new Blob(chunks);
    if (await sha256(await blob.arrayBuffer()) !== file.sha256) throw new Error(`Desktop legacy file changed: ${file.path}`);
    files.set(file.path, blob); hashes.set(file.path, file.sha256);
  }
  if ((await source.inventory()).fingerprint !== inventory.fingerprint) throw new Error('Desktop legacy workspace changed during capture. Keep the originals and retry.');
  const sessions: { key: string; value: unknown }[] = [], trash: typeof sessions = [], tombstones: typeof sessions = [], aliases: typeof sessions = [];
  const blobs = new Map<string, { blob: Blob; hash: string }>();
  async function decode(value: unknown, root: string): Promise<unknown> {
    if (!value || typeof value !== 'object') return value;
    if (Array.isArray(value)) return Promise.all(value.map((item) => decode(item, root)));
    const object = value as Record<string, unknown>;
    if (typeof object.$typed === 'string') {
      let data = object.b64;
      if (typeof object.blob === 'string') {
        const blob = files.get(`${root}/blobs/${object.blob}`);
        if (!blob) throw new Error('Desktop geometry bytes are missing');
        data = bytesBase64(new Uint8Array(await blob.arrayBuffer()));
      }
      if (typeof data !== 'string') throw new Error('Invalid desktop typed-array field');
      return parseDocument(JSON.stringify({ $orcaBinary: object.$typed, data }));
    }
    if (typeof object.$date === 'number' && Object.keys(object).length === 1) return new Date(object.$date);
    const result: Record<string, unknown> = Object.create(null);
    for (const [key, item] of Object.entries(object)) result[key] = await decode(item, root);
    return result;
  }
  for (const [path, blob] of files) {
    const [rootName, ...relative] = path.split('/');
    const familyName = inventory.roots.find((root) => root.paths.includes(rootName))?.name;
    if (!familyName) throw new Error('Desktop file is outside the inventoried root families');
    const logical = [familyName, ...relative].join('/');
    const match = /^(projects|trash)\/([^/]+)\/project\.json$/.exec(logical);
    if (match) {
      const [, family, id] = match, root = path.slice(0, -'/project.json'.length), raw = JSON.parse(await blob.text()) as { format: number; meta: Record<string, unknown>; doc: unknown };
      if (raw.format !== 1 || !raw.meta || !raw.doc) throw new Error(`Unresolved desktop project: ${id}`);
      let thumbnail: string | null = null;
      const thumb = files.get(`${root}/thumb.jpg`);
      if (thumb) { const bytes = new Uint8Array(await thumb.arrayBuffer()); thumbnail = `data:${bytes[0] === 0x89 ? 'image/png' : bytes[0] === 0x52 ? 'image/webp' : 'image/jpeg'};base64,${bytesBase64(bytes)}`; }
      (family === 'projects' ? sessions : trash).push({ key: id, value: { ...raw.meta, id, thumbnail, doc: await decode(raw.doc, root) } });
    }
    const asset = /^(?:projects|trash)\/[^/]+\/blobs\/([^/]+)$/.exec(logical);
    if (asset) {
      const previous = blobs.get(asset[1]);
      if (previous && previous.hash !== hashes.get(path)) throw new Error(`Desktop asset key has conflicting bytes: ${asset[1]}`);
      blobs.set(asset[1], { blob, hash: hashes.get(path)! });
    }
    const marker = /^deleted-projects\/([^/]+)\.json$/.exec(logical);
    if (marker) tombstones.push({ key: marker[1], value: JSON.parse(await blob.text()) });
    if (logical === 'project-aliases.json') {
      const raw = JSON.parse(await blob.text()) as Record<string, unknown>;
      if (!raw || typeof raw !== 'object' || Array.isArray(raw)) throw new Error('Invalid desktop legacy aliases');
      for (const [key, value] of Object.entries(raw)) aliases.push({ key, value });
    }
  }
  const deleted = new Set(tombstones.map(({ key }) => key));
  for (let index = sessions.length - 1; index >= 0; index--) if (deleted.has(sessions[index].key)) trash.push(...sessions.splice(index, 1));
  return { inventory, files: [...files].map(([path, blob]) => ({ path, blob })), database: { name: 'desktop-projects', version: 1,
    stores: [{ name: 'sessions', records: sessions }, { name: 'trash', records: trash }, { name: 'tombstones', records: tombstones },
      { name: 'aliases', records: aliases }, { name: 'blobs', records: [...blobs].map(([key, { blob }]) => ({ key, value: blob })) }] } };
}
