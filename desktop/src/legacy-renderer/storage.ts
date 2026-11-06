

import { createHash } from 'node:crypto';
import { constants, type BigIntStats } from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import type { RendererDatabase, RendererDomainCoverage, RendererOriginInventory } from '../legacy-renderer';
import { Budget, Cursor, currentView, Incomplete, LIMITS, requireSource, type CurrentRecord, type UserComparator } from './leveldb';
import { recoverIDB } from './indexeddb';
import { idbComparator } from './indexeddb-comparator';
import { Parts, screenStorage, utf8, utf16 } from './values';

const stamp = (s: BigIntStats) => [s.dev, s.ino, s.mode, s.size, s.mtimeNs, s.ctimeNs].join(':');
const sha = (b: Buffer | string) => createHash('sha256').update(b).digest('hex');
const sort = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;
type Observation = { stamp: string; names?: string[]; sha256?: string };

class Snapshot {
  private readonly observations = new Map<string, Observation>();
  private readonly contents = new Map<string, Buffer>();
  private bytes = 0;
  private entries = 0;
  constructor(private readonly root: string) {}

  private async ancestors(relative: string) {
    requireSource(!path.isAbsolute(relative) && relative.split('/').every((s) => s !== '..' && s !== '.' && !s.includes('\\')));
    const full = path.join(this.root, relative), parsed = path.parse(full);
    let current = parsed.root;
    for (const segment of full.slice(parsed.root.length).split(path.sep).slice(0, -1)) {
      current = path.join(current, segment);
      const s = await fs.lstat(current, { bigint: true });
      if (!s.isDirectory() || s.isSymbolicLink()) throw new Incomplete('source_changed', 'Unsupported source ancestor.');
    }
    return full;
  }
  async directory(relative: string): Promise<string[]> {
    const full = await this.ancestors(relative), before = await fs.lstat(full, { bigint: true });
    if (!before.isDirectory() || before.isSymbolicLink()) throw new Incomplete('unsupported_layout', 'Unsupported source directory.');
    const names = (await fs.readdir(full)).sort(sort);
    this.entries += names.length; requireSource(this.entries <= LIMITS.files, 'Source entry budget exceeded.');
    requireSource(names.every((n) => n && n !== '.' && n !== '..' && !/[\\/\x00-\x1f]/.test(n)));
    requireSource(stamp(before) === stamp(await fs.lstat(full, { bigint: true })), 'Source directory changed.');
    this.observations.set(relative, { stamp: stamp(before), names }); return names;
  }
  async file(relative: string): Promise<Buffer> {
    const cached = this.contents.get(relative); if (cached) return cached;
    const result = await this.read(relative);
    this.contents.set(relative, result.bytes); this.observations.set(relative, { stamp: result.stamp, sha256: sha(result.bytes) });
    return result.bytes;
  }
  private async metadata(relative: string) {
    const s = await fs.lstat(await this.ancestors(relative), { bigint: true });
    requireSource(s.isFile() && !s.isSymbolicLink() && s.nlink === 1n, 'Unsupported database directory entry.');
    const old = this.observations.get(relative);
    requireSource(!old || old.stamp === stamp(s), 'Source entry changed during enumeration.');


    if (!old) this.observations.set(relative, { stamp: stamp(s) });
  }
  private async read(relative: string) {
    const full = await this.ancestors(relative), before = await fs.lstat(full, { bigint: true });
    requireSource(before.isFile() && !before.isSymbolicLink() && before.nlink === 1n && before.size <= BigInt(LIMITS.file), 'Unsupported source file.');
    this.bytes += Number(before.size); requireSource(this.bytes <= LIMITS.bytes, 'Source byte budget exceeded.');
    const h = await fs.open(full, constants.O_RDONLY | constants.O_NOFOLLOW);
    try {
      requireSource(stamp(before) === stamp(await h.stat({ bigint: true })), 'Source file identity changed.');
      const bytes = Buffer.alloc(Number(before.size)); let at = 0;
      while (at < bytes.length) {
        const r = await h.read(bytes, at, bytes.length - at, at); requireSource(r.bytesRead > 0, 'Short source read.'); at += r.bytesRead;
      }
      requireSource(stamp(before) === stamp(await h.stat({ bigint: true }))
        && stamp(before) === stamp(await fs.lstat(await this.ancestors(relative), { bigint: true })), 'Source file changed.');
      return { bytes, stamp: stamp(before) };
    } finally { await h.close(); }
  }
  async verify(): Promise<string> {

    for (const [relative, old] of this.observations) {
      if (old.names) {
        const full = await this.ancestors(relative), stat = await fs.lstat(full, { bigint: true });
        requireSource(stat.isDirectory() && !stat.isSymbolicLink() && stamp(stat) === old.stamp
          && JSON.stringify((await fs.readdir(full)).sort(sort)) === JSON.stringify(old.names), 'Source directory changed.');
      } else if (old.sha256) {
        const now = await this.read(relative);
        if (now.stamp !== old.stamp || sha(now.bytes) !== old.sha256) throw new Incomplete('source_changed', 'Source bytes changed.');
      } else {
        const s = await fs.lstat(await this.ancestors(relative), { bigint: true });
        if (!s.isFile() || s.isSymbolicLink() || stamp(s) !== old.stamp) throw new Incomplete('source_changed', 'Database directory entry changed.');
      }
    }
    return sha(JSON.stringify([...this.observations].sort(([a], [b]) => sort(a, b))));
  }
  async database(relative: string, comparator?: UserComparator): Promise<CurrentRecord[]> {
    const names = await this.directory(relative);

    requireSource(names.every((n) => /^(?:CURRENT|LOCK|LOG(?:\.old)?|MANIFEST-[0-9]{6,}|[0-9]{6,}\.(?:ldb|sst|log))$/.test(n)),
      'Unknown database file family.');
    for (const name of names) await this.metadata(`${relative}/${name}`);
    return currentView(names, (n) => this.file(`${relative}/${n}`), new Budget(), comparator);
  }
  async blobTree(relative: string): Promise<string[]> {
    const files: string[] = [];
    for (const db of await this.directory(relative)) {
      requireSource(/^[1-9a-f][0-9a-f]*$/.test(db), 'Unknown external-object database directory.');
      for (const shard of await this.directory(`${relative}/${db}`)) {
        requireSource(/^[0-9a-f]{2}$/.test(shard), 'Unknown external-object shard.');
        for (const file of await this.directory(`${relative}/${db}/${shard}`)) {
          requireSource(/^[1-9a-f][0-9a-f]*$/.test(file), 'Unknown external-object filename.');
          const name = `${relative}/${db}/${shard}/${file}`; await this.metadata(name); files.push(name);
        }
      }
    }
    return files;
  }
}

function exactOrigin(raw: string): string {
  const match = /^http:\/\/127\.0\.0\.1:([1-9][0-9]{0,4})$/.exec(raw);
  if (!match || Number(match[1]) > 65535) throw new Incomplete('invalid_origin', 'Unsupported original storage key or origin.');
  return raw;
}
function latinOrUTF16(raw: Buffer) {
  requireSource(raw.length > 0);
  if (raw[0] === 0) return utf16(raw.subarray(1));
  if (raw[0] === 1) return raw.subarray(1).toString('latin1');
  throw new Incomplete('unsupported_type', 'Unsupported storage string encoding.');
}

function localStorage(records: CurrentRecord[]): Map<string, { key: string; value: string }[]> {
  const origins = new Map<string, { key: string; value: string }[]>(), seen = new Set<string>();
  let version = false;
  for (const record of records) {
    if (record.key.equals(Buffer.from('VERSION'))) { requireSource(record.value.equals(Buffer.from('1'))); version = true; continue; }
    if (record.key.subarray(0, 5).equals(Buffer.from('META:'))) {
      const origin = exactOrigin(utf8(record.key.subarray(5))), c = new Cursor(record.value);
      requireSource(c.number() === 8); c.varint(); requireSource(c.number() === 16); c.varint(); c.done();
      if (!origins.has(origin)) origins.set(origin, []);
    } else if (record.key[0] === 95) {
      const at = record.key.indexOf(0, 1); requireSource(at > 1);
      const origin = exactOrigin(utf8(record.key.subarray(1, at))), key = latinOrUTF16(record.key.subarray(at + 1));
      const value = latinOrUTF16(record.value), unique = JSON.stringify([origin, key]);
      requireSource(!seen.has(unique), 'Ambiguous logical storage key.'); seen.add(unique);
      screenStorage(key, value);
      if (!origins.has(origin)) origins.set(origin, []);
      origins.get(origin)!.push({ key, value });
    } else throw new Incomplete('unsupported_type', 'Unknown local storage metadata.');
  }
  requireSource(version, 'Unsupported local storage schema.');
  for (const values of origins.values()) values.sort((a, b) => sort(a.key, b.key));
  return origins;
}

function sessionStorage(records: CurrentRecord[]): Map<string, { namespaceId: string; values: { key: string; value: string }[] }[]> {
  const namespaces: { id: string; origin: string; map: string }[] = [], maps = new Map<string, { key: string; value: string }[]>();
  const referenced = new Map<string, string>(), namespaceKeys = new Set<string>();
  let version = false;
  for (const record of records) {
    const key = utf8(record.key);
    if (key === 'version') { requireSource(record.value.equals(Buffer.from('1'))); version = true; continue; }
    if (key === 'next-map-id') { requireSource(/^(?:0|[1-9][0-9]*)$/.test(utf8(record.value))); continue; }
    const namespace = /^namespace-([0-9a-fA-F]{32}|[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})-(http:\/\/127\.0\.0\.1:[0-9]+)$/.exec(key);
    if (namespace) {
      const id = namespace[1], origin = exactOrigin(namespace[2]), map = utf8(record.value), unique = JSON.stringify([id, origin]);
      requireSource(/^(?:0|[1-9][0-9]*)$/.test(map) && !namespaceKeys.has(unique)); namespaceKeys.add(unique);
      requireSource(!referenced.has(map) || referenced.get(map) === origin, 'Cross-origin shared session map.');
      referenced.set(map, origin); namespaces.push({ id, origin, map });
    } else {
      const item = /^map-(0|[1-9][0-9]*)-([\s\S]*)$/.exec(key);
      if (!item) throw new Incomplete('unsupported_type', 'Unresolved session namespace or map metadata.');
      const value = utf16(record.value); screenStorage(item[2], value);
      const list = maps.get(item[1]) ?? [];
      requireSource(!list.some((v) => v.key === item[2]), 'Ambiguous session key.');
      list.push({ key: item[2], value }); maps.set(item[1], list);
    }
  }
  requireSource(version, 'Unsupported session storage schema.');
  for (const map of maps.keys()) requireSource(referenced.has(map), 'Unresolved live session map.');
  const output = new Map<string, { namespaceId: string; values: { key: string; value: string }[] }[]>();
  for (const n of namespaces.sort((a, b) => sort(a.id, b.id) || sort(a.origin, b.origin))) {
    const list = output.get(n.origin) ?? [];

    list.push({ namespaceId: n.id, values: (maps.get(n.map) ?? []).map(({ key, value }) => ({ key, value })).sort((a, b) => sort(a.key, b.key)) });
    output.set(n.origin, list);
  }
  return output;
}



const excluded = new Set(['data', 'jobs', 'logs', 'projects', 'trash', 'deleted-projects', 'project-aliases.json', 'assets',
  'Local State', 'Preferences', 'Secure Preferences', 'Cookies', 'Cookies-journal', 'Network', 'Login Data', 'Login Data-journal',
  'Web Data', 'Web Data-journal', 'Cache', 'Code Cache', 'GPUCache', 'DawnCache', 'DawnGraphiteCache', 'DawnWebGPUCache',
  'Dictionaries', 'Crashpad', 'Crash Reports', 'TransportSecurity', 'Network Persistent State', 'CertificateRevocation',
  'SingletonLock', 'SingletonCookie', 'SingletonSocket', 'DevToolsActivePort', 'chrome_debug.log', 'lockfile', 'browsermetrics',
  'BrowserMetrics', 'GrShaderCache', 'ShaderCache']);
const domains = new Set(['Local Storage', 'Session Storage', 'IndexedDB', 'blob_storage']);

export interface DecodedSnapshot {
  coverage: RendererDomainCoverage;
  absence: { supportedLayout: string; sourceRefs: string[] } | null;
  origins: RendererOriginInventory[];
  physicalFingerprint: string;

  parts: Map<string, { origin: string; bytes: Buffer; size: number; sha256: string }>;
}
export async function scanOriginal(root: string): Promise<DecodedSnapshot> {
  const snapshot = new Snapshot(root), names = await snapshot.directory('');
  requireSource(names.every((name) => domains.has(name) || excluded.has(name)), 'Unrecognized original session layout or alternate storage root.');
  let local = new Map<string, { key: string; value: string }[]>();
  let session = new Map<string, { namespaceId: string; values: { key: string; value: string }[] }[]>();
  const databases = new Map<string, RendererDatabase[]>(), originParts = new Map<string, Parts>();
  const partsFor = (origin: string) => {
    if (!originParts.has(origin)) originParts.set(origin, new Parts(origin)); return originParts.get(origin)!;
  };
  if (names.includes('Local Storage')) {
    const children = await snapshot.directory('Local Storage');
    requireSource(children.length === 1 && children[0] === 'leveldb', 'Legacy SQLite or alternate local storage layout.');
    local = localStorage(await snapshot.database('Local Storage/leveldb'));
  }
  if (names.includes('Session Storage')) session = sessionStorage(await snapshot.database('Session Storage'));
  if (names.includes('IndexedDB')) {
    const children = await snapshot.directory('IndexedDB');
    requireSource(children.every((n) => /^http_127\.0\.0\.1_[1-9][0-9]{0,4}\.indexeddb\.(leveldb|blob)$/.test(n)),
      'Unknown IndexedDB partition, bucket, SQLite or source layout.');
    const ownedBlobRoots = new Set<string>();
    for (const directory of children.filter((n) => n.endsWith('.leveldb'))) {
      const blobName = directory.slice(0, -'.leveldb'.length) + '.blob', blobRoot = `IndexedDB/${blobName}`;
      const current = await snapshot.database(`IndexedDB/${directory}`, idbComparator);
      const recovered = await recoverIDB(current, partsFor, (relative) => snapshot.file(relative), blobRoot);


      requireSource(directory === recovered.identifier.replace(/@1$/, '') + '.indexeddb.leveldb'
        && !databases.has(recovered.origin), 'IndexedDB metadata/layout ownership differs.');
      exactOrigin(recovered.origin); databases.set(recovered.origin, recovered.databases);
      if (children.includes(blobName)) {
        ownedBlobRoots.add(blobName);
        const physical = await snapshot.blobTree(blobRoot);
        requireSource(physical.length === recovered.ownedBlobFiles.size && physical.every((n) => recovered.ownedBlobFiles.has(n)),
          'Unowned or missing external-object files.');
      } else requireSource(recovered.ownedBlobFiles.size === 0, 'External-object source directory is missing.');
    }
    requireSource(children.filter((n) => n.endsWith('.blob')).every((n) => ownedBlobRoots.has(n)), 'Unlinked external-object directory.');
  }
  if (names.includes('blob_storage')) requireSource((await snapshot.directory('blob_storage')).length === 0, 'Unresolved persisted blob data.');
  const parts: DecodedSnapshot['parts'] = new Map(); let partBytes = 0;
  for (const [origin, p] of originParts) for (const [key, bytes] of p.bytes) {
    partBytes += bytes.length; requireSource(partBytes <= LIMITS.logical && !parts.has(key));
    parts.set(key, { origin, bytes, size: bytes.length, sha256: sha(bytes) });
  }
  const coverage: RendererDomainCoverage = {
    localStorage: names.includes('Local Storage') ? 'decoded' : 'verified-absent',
    indexedDB: databases.size ? 'decoded' : 'verified-absent', sessionStorage: names.includes('Session Storage') ? 'decoded' : 'verified-absent', blobs: parts.size ? 'decoded' : 'verified-absent',
  };
  const origins: RendererOriginInventory[] = [...new Set([...local.keys(), ...session.keys(), ...databases.keys()])].sort(sort).map((origin) => ({
    origin, coverage: { localStorage: local.has(origin) ? 'decoded' : 'verified-absent', indexedDB: databases.has(origin) ? 'decoded' : 'verified-absent',
      sessionStorage: session.has(origin) ? 'decoded' : 'verified-absent', blobs: originParts.get(origin)?.bytes.size ? 'decoded' : 'verified-absent' },
    localStorage: local.get(origin) ?? [], databases: databases.get(origin) ?? [], sessionStorageNamespaces: session.get(origin) ?? [],
    binaryParts: [...parts].filter(([, p]) => p.origin === origin).map(([key, p]) => ({ key, size: p.size, sha256: p.sha256 })),
  }));
  requireSource(Buffer.byteLength(JSON.stringify(origins)) <= LIMITS.logical, 'Logical inventory budget exceeded.');
  const sourceRefs = (['localStorage', 'indexedDB', 'sessionStorage', 'blobs'] as const)
    .filter((domain) => coverage[domain] === 'verified-absent')
    .map((domain) => ({ localStorage: 'Local Storage', indexedDB: 'IndexedDB', sessionStorage: 'Session Storage', blobs: 'blob_storage' })[domain]);
  if (coverage.blobs === 'verified-absent' && names.includes('IndexedDB') && !sourceRefs.includes('IndexedDB')) sourceRefs.push('IndexedDB');
  return { coverage, absence: sourceRefs.length ? { supportedLayout: 'a85-default-session-direct-root-webstorage-v1', sourceRefs } : null,
    origins, physicalFingerprint: await snapshot.verify(), parts };
}
