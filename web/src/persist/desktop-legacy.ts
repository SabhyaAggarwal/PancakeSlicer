import type { LegacyDatabase } from './legacy-projects';
import { bytesBase64, parseDocument, sha256 } from './wire';
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
    const result: Record<string, unknown> = {};
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
