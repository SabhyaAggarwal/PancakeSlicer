import type { RendererDatabase, RendererDecodedValue } from '../legacy-renderer';
import { Cursor, Incomplete, LIMITS, requireSource, type CurrentRecord } from './leveldb';
import { Parts, screenLogical, utf16 } from './values';
import { encodedKey, prefix, stringBytes } from './indexeddb-comparator';
import { approveExternal, blinkValue, externalObjects } from './indexeddb-blobs';

export function decodeIDBKey(bytes: Buffer, parts: Parts): RendererDecodedValue {
  const c = new Cursor(bytes); let nodes = 0;
  const key = (depth: number): RendererDecodedValue => {
    requireSource(depth <= LIMITS.depth && ++nodes <= LIMITS.records);
    const tag = c.byte();
    if (tag === 1) { const length = c.number(); requireSource(length <= LIMITS.text / 2); return utf16(c.take(length * 2), true); }
    if (tag === 2 || tag === 3) {
      const value = c.take(8).readDoubleLE(); requireSource(Number.isFinite(value) && !Object.is(value, -0));
      if (tag === 2) { requireSource(Number.isInteger(value) && Math.abs(value) <= 8.64e15); return { $orcaDate: value }; }
      return value;
    }
    if (tag === 4) {
      const length = c.number(); requireSource(length <= LIMITS.records);
      const result: RendererDecodedValue[] = []; for (let i = 0; i < length; i++) result.push(key(depth + 1));
      return result;
    }
    if (tag === 6) return parts.add(c.sized(), 'ArrayBuffer');
    throw new Incomplete('unsupported_type', 'Unsupported IndexedDB key.');
  };
  const result = key(0); c.done(); return result;
}

const integer = (raw: Buffer) => {
  requireSource(raw.length > 0 && raw.length <= 8); let n = 0n;
  for (let i = 0; i < raw.length; i++) n |= BigInt(raw[i]) << BigInt(i * 8);
  requireSource(n < (1n << 63n)); return n;
};
const variable = (raw: Buffer) => { const c = new Cursor(raw), n = c.varint(); c.done(); requireSource(n < (1n << 63n)); return n; };
const jsonInteger = (n: bigint): RendererDecodedValue => n <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(n) : n.toString();
const bool = (raw: Buffer) => { requireSource(raw.length === 1 && raw[0] <= 1); return raw[0] === 1; };
function keyPath(raw: Buffer): RendererDecodedValue {
  if (raw.length < 3 || raw[0] !== 0 || raw[1] !== 0) return utf16(raw, true);
  const c = new Cursor(raw); c.take(2); const tag = c.byte();
  let result: RendererDecodedValue;
  if (tag === 0) result = null;
  else if (tag === 1) result = utf16(stringBytes(c), true);
  else {
    requireSource(tag === 2); const count = c.number(); requireSource(count <= LIMITS.records);
    const items: string[] = []; for (let i = 0; i < count; i++) items.push(utf16(stringBytes(c), true)); result = items;
  }
  c.done(); return result;
}
interface Store {
  fields: Map<number, Buffer>; indexes: Map<bigint, Map<number, Buffer>>;
  rows: { key: Buffer; value: Buffer }[]; exists: Map<string, Buffer>; blobs: Map<string, { key: Buffer; value: Buffer }>;
  indexRows: { index: bigint; key: Buffer; value: Buffer }[];
  names: Map<string, bigint>; freeIndexes: bigint[];
}
interface Database { fields: Map<number, Buffer>; stores: Map<bigint, Store>; names: Map<string, bigint>; freeStores: bigint[] }
export interface IDBRecovery { origin: string; identifier: string; databases: RendererDatabase[]; ownedBlobFiles: Set<string> }

export async function recoverIDB(records: CurrentRecord[], partsFor: (origin: string) => Parts,
  read: (relative: string) => Promise<Buffer>, blobRoot: string): Promise<IDBRecovery> {
  const global: Record<string, RendererDecodedValue> = Object.create(null);
  const globals = new Map<number, Buffer>(), links = new Map<bigint, { name: string; identifier: string }>();
  const dbs = new Map<bigint, Database>(), ownedBlobFiles = new Set<string>();
  const db = (id: bigint) => {
    requireSource(id > 0n); if (!dbs.has(id)) dbs.set(id, { fields: new Map(), stores: new Map(), names: new Map(), freeStores: [] }); return dbs.get(id)!;
  };
  const store = (d: Database, id: bigint) => {
    requireSource(id > 0n); if (!d.stores.has(id)) d.stores.set(id, { fields: new Map(), indexes: new Map(), rows: [],
      exists: new Map(), blobs: new Map(), indexRows: [], names: new Map(), freeIndexes: [] }); return d.stores.get(id)!;
  };
  const put = <K>(map: Map<K, Buffer>, key: K, value: Buffer) => { requireSource(!map.has(key), 'Duplicate logical IndexedDB metadata.'); map.set(key, value); };
  for (const row of records) {
    const c = new Cursor(row.key), p = prefix(c);
    if (p.database === 0n) {
      requireSource(p.store === 0n && p.index === 0n); const tag = c.byte();
      if (tag <= 6) { c.done(); put(globals, tag, row.value); }
      else if (tag === 50) {
        requireSource(c.byte() === 0, 'Pending scopes/undo/cleanup state requires recovery.'); c.done();
        const v = new Cursor(row.value); requireSource(v.number() === 8 && v.number() === 1); v.done(); global.scopesVersion = 1;
      } else if (tag === 100) {
        const id = c.varint(); c.done(); requireSource(id > 0n && id < (1n << 63n) && row.value.length === 0);
        global.freeDatabaseIds ??= [];
        (global.freeDatabaseIds as RendererDecodedValue[]).push(jsonInteger(id));
      } else {
        requireSource(tag === 201); const identifier = utf16(stringBytes(c), true), name = utf16(stringBytes(c), true); c.done();
        const id = integer(row.value); requireSource(id > 0n && !links.has(id));
        requireSource(['orca-profile-library', 'orca-web', 'orca-web-drafts'].includes(name), 'Unowned IndexedDB database.');
        links.set(id, { name, identifier });
      }
      continue;
    }
    const d = db(p.database);
    if (p.store === 0n) {
      requireSource(p.index === 0n); const tag = c.byte();
      if (tag <= 5) { c.done(); put(d.fields, tag, row.value); }
      else if (tag === 50) { const s = store(d, c.varint()), field = c.byte(); c.done(); requireSource(field <= 7); put(s.fields, field, row.value); }
      else if (tag === 100) {
        const s = store(d, c.varint()), id = c.varint(), field = c.byte(); c.done(); requireSource(id >= 30n && field <= 3);
        if (!s.indexes.has(id)) s.indexes.set(id, new Map()); put(s.indexes.get(id)!, field, row.value);
      } else if (tag === 150) { const id = c.varint(); c.done(); requireSource(id > 0n && id < (1n << 63n) && row.value.length === 0); d.freeStores.push(id); }
      else if (tag === 151) { const s = store(d, c.varint()), id = c.varint(); c.done(); requireSource(row.value.length === 0); s.freeIndexes.push(id); }
      else if (tag === 200) {
        const name = utf16(stringBytes(c), true); c.done(); requireSource(!d.names.has(name)); d.names.set(name, integer(row.value));
      } else {
        requireSource(tag === 201); const s = store(d, c.varint()), name = utf16(stringBytes(c), true); c.done();
        requireSource(!s.names.has(name)); s.names.set(name, integer(row.value));
      }
    } else {
      const s = store(d, p.store), key = row.key.subarray(c.at);
      if (p.index === 1n || p.index === 2n || p.index === 3n) {
        encodedKey(c); c.done();
        if (p.index === 1n) s.rows.push({ key, value: row.value });
        else if (p.index === 2n) put(s.exists, key.toString('hex'), row.value);
        else { requireSource(!s.blobs.has(key.toString('hex'))); s.blobs.set(key.toString('hex'), { key, value: row.value }); }
      } else { requireSource(p.index >= 30n); s.indexRows.push({ index: p.index, key, value: row.value }); }
    }
  }
  requireSource(globals.has(0) && globals.has(1) && globals.has(2) && links.size > 0, 'Unresolved IndexedDB global metadata or origin.');
  const schema = integer(globals.get(0)!); requireSource(schema === 4n || schema === 5n, 'Unsupported IndexedDB schema version.');
  const format = integer(globals.get(2)!), v8 = format >> 32n, blink = format & 0xffffffffn;
  requireSource((v8 === 15n || v8 === 16n) && blink >= 16n && blink <= 21n, 'Unsupported IndexedDB data format.');
  global.schemaVersion = Number(schema); global.dataVersion = jsonInteger(format); global.maxDatabaseId = jsonInteger(integer(globals.get(1)!));
  for (const tag of [3, 4]) if (globals.has(tag)) {
    requireSource(globals.get(tag)!.length === 0, 'Pending blob journal requires native recovery.'); global[tag === 3 ? 'recoveryBlobJournal' : 'activeBlobJournal'] = [];
  }
  for (const tag of [5, 6]) if (globals.has(tag)) global[tag === 5 ? 'earliestSweepTime' : 'earliestCompactionTime'] = jsonInteger(integer(globals.get(tag)!));
  const identifiers = new Set([...links.values()].map((link) => link.identifier)); requireSource(identifiers.size === 1, 'Mixed original storage origins.');
  const identifier = [...identifiers][0], originMatch = /^http_127\.0\.0\.1_([1-9][0-9]{0,4})@1$/.exec(identifier);
  requireSource(originMatch && Number(originMatch[1]) <= 65535, 'Unsupported storage key, partition or original origin.');
  const origin = `http://127.0.0.1:${originMatch[1]}`; requireSource(new URL(origin).origin === origin);
  const parts = partsFor(origin), output: RendererDatabase[] = [], names = new Set<string>();
  requireSource([...dbs.keys()].every((id) => links.has(id)), 'Unlinked live database metadata.');
  for (const [id, link] of links) {
    requireSource(!names.has(link.name) && id <= integer(globals.get(1)!)); names.add(link.name);
    requireSource(!(global.freeDatabaseIds as RendererDecodedValue[] | undefined)?.includes(jsonInteger(id)), 'Live database is marked free.');
    const d = dbs.get(id); requireSource(d && d.fields.has(4), 'Missing live database metadata.');
    const version = variable(d.fields.get(4)!); requireSource(version > 0n && version <= BigInt(Number.MAX_SAFE_INTEGER), 'Unfinished or unsupported database version.');
    const metadata: Record<string, RendererDecodedValue> = { id: jsonInteger(id), originIdentifier: link.identifier,
      global: JSON.parse(JSON.stringify(global)) as RendererDecodedValue };
    if (d.fields.has(0)) { metadata.originName = utf16(d.fields.get(0)!, true); requireSource(metadata.originName === identifier); }
    if (d.fields.has(1)) { metadata.databaseName = utf16(d.fields.get(1)!, true); requireSource(metadata.databaseName === link.name); }
    if (d.fields.has(2)) metadata.userStringVersion = utf16(d.fields.get(2)!, true);
    if (d.fields.has(3)) metadata.maxObjectStoreId = jsonInteger(integer(d.fields.get(3)!));
    if (d.fields.has(5)) metadata.blobNumberGenerator = jsonInteger(variable(d.fields.get(5)!));
    if (d.freeStores.length) metadata.freeObjectStoreIds = d.freeStores.map(jsonInteger);
    const stores: RendererDatabase['stores'] = [], seenNames = new Set<string>();
    for (const [sid, s] of d.stores) {
      requireSource(s.fields.has(0) && !d.freeStores.includes(sid), 'Unresolved live object store.');
      const name = utf16(s.fields.get(0)!, true); requireSource(name && !seenNames.has(name)); seenNames.add(name);
      if (d.fields.has(3)) requireSource(sid <= integer(d.fields.get(3)!));
      const meta: Record<string, RendererDecodedValue> = { id: jsonInteger(sid) };
      if (s.fields.has(1)) meta.keyPath = keyPath(s.fields.get(1)!);
      if (s.fields.has(2)) meta.autoIncrement = bool(s.fields.get(2)!);
      if (s.fields.has(3)) meta.evictable = bool(s.fields.get(3)!);
      for (const [tag, field] of [[4, 'lastVersion'], [5, 'maxIndexId'], [7, 'keyGeneratorCurrentNumber']] as const)
        if (s.fields.has(tag)) meta[field] = jsonInteger(integer(s.fields.get(tag)!));
      if (s.fields.has(6)) { meta.hasKeyPath = bool(s.fields.get(6)!); requireSource(meta.hasKeyPath || meta.keyPath === null || meta.keyPath === ''); }
      const indexes: Record<string, RendererDecodedValue>[] = [], indexNames = new Set<string>();
      for (const [iid, fields] of s.indexes) {
        requireSource(fields.has(0) && !s.freeIndexes.includes(iid));
        const index: Record<string, RendererDecodedValue> = { id: jsonInteger(iid), name: utf16(fields.get(0)!, true) };
        requireSource(typeof index.name === 'string' && !indexNames.has(index.name)); indexNames.add(index.name);
        if (s.fields.has(5)) requireSource(iid <= integer(s.fields.get(5)!));
        if (fields.has(1)) index.unique = bool(fields.get(1)!);
        if (fields.has(2)) index.keyPath = keyPath(fields.get(2)!);
        if (fields.has(3)) index.multiEntry = bool(fields.get(3)!);
        if (s.names.has(index.name as string)) requireSource(s.names.get(index.name as string) === iid);
        indexes.push(index);
      }
      if (indexes.length) meta.indexes = indexes.sort((a, b) => (a.name as string) < (b.name as string) ? -1 : 1);
      if (s.freeIndexes.length) meta.freeIndexIds = s.freeIndexes.map(jsonInteger);
      for (const [name, iid] of s.names) requireSource(s.indexes.has(iid) && utf16(s.indexes.get(iid)!.get(0)!, true) === name, 'Unresolved index-name mapping.');
      const rows: RendererDatabase['stores'][number]['records'] = [], versions = new Map<string, bigint>();
      for (const row of s.rows) {
        const keyHex = row.key.toString('hex'), v = new Cursor(row.value), revision = v.varint();
        requireSource(revision > 0n && s.exists.has(keyHex) && integer(s.exists.get(keyHex)!) === revision, 'Record/existence version mismatch.');
        if (s.fields.has(4)) requireSource(revision <= integer(s.fields.get(4)!), 'Record version exceeds store metadata.');
        versions.set(keyHex, revision);
        const objects = s.blobs.has(keyHex) ? await externalObjects(s.blobs.get(keyHex)!.value, id, blobRoot, read, ownedBlobFiles) : [];
        const key = decodeIDBKey(row.key, parts), value = blinkValue(v.take(row.value.length - v.at), objects, parts);
        screenLogical(key, parts); screenLogical(value, parts); rows.push({ key, value });
        if (objects.length) {
          meta.externalObjectRecords ??= [];
          (meta.externalObjectRecords as RendererDecodedValue[]).push({ key: decodeIDBKey(row.key, parts), objects: objects.map((o) => ({
            kind: o.kind, blobNumber: o.blobNumber, sourceRef: o.sourceRef, mimeType: o.mimeType, size: o.size, serializedValueWrapper: o.wrapper,
            ...(o.name === undefined ? {} : { name: o.name, lastModified: o.lastModified!, lastModifiedMicros: o.lastModifiedMicros! }),
          })) });
        }
        const unused = objects.filter((o) => !o.used).map((o) => approveExternal(o, parts));
        if (unused.length) meta.orphanExternalObjects ??= [];
        if (unused.length) (meta.orphanExternalObjects as RendererDecodedValue[]).push({ key: decodeIDBKey(row.key, parts), parts: unused });
      }
      requireSource([...s.exists.keys()].every((key) => versions.has(key)), 'Orphan live existence record.');
      for (const [keyHex, row] of s.blobs) if (!versions.has(keyHex)) {
        const objects = await externalObjects(row.value, id, blobRoot, read, ownedBlobFiles);
        const refs = objects.map((o) => approveExternal(o, parts));
        meta.externalObjectRecords ??= [];
        (meta.externalObjectRecords as RendererDecodedValue[]).push({ key: decodeIDBKey(row.key, parts), objects: objects.map((o) => ({
          kind: o.kind, blobNumber: o.blobNumber, sourceRef: o.sourceRef, mimeType: o.mimeType, size: o.size, serializedValueWrapper: false,
          ...(o.name === undefined ? {} : { name: o.name, lastModified: o.lastModified!, lastModifiedMicros: o.lastModifiedMicros! }),
        })) });
        meta.orphanExternalObjects ??= [];
        (meta.orphanExternalObjects as RendererDecodedValue[]).push({ key: decodeIDBKey(row.key, parts), parts: refs });
      }
      const indexEntries: RendererDecodedValue[] = [];
      for (const row of s.indexRows) {
        requireSource(s.indexes.has(row.index), 'Unowned live index data.');
        const c = new Cursor(row.key), begin = c.at; encodedKey(c); const indexKey = c.bytes.subarray(begin, c.at);
        const sequence = c.at < row.key.length ? c.varint() : null;
        let primary: Buffer | undefined;
        if (c.at < row.key.length) { const start = c.at; encodedKey(c); primary = c.bytes.subarray(start, c.at); } c.done();
        const v = new Cursor(row.value), revision = v.varint(), start = v.at; encodedKey(v); const valuePrimary = v.bytes.subarray(start, v.at); v.done();
        requireSource(!primary || primary.equals(valuePrimary), 'Index primary-key mapping differs.');
        indexEntries.push({ indexId: jsonInteger(row.index), key: decodeIDBKey(indexKey, parts), primaryKey: decodeIDBKey(valuePrimary, parts),
          ...(sequence === null ? {} : { sequence: jsonInteger(sequence) }), revision: jsonInteger(revision), current: versions.get(valuePrimary.toString('hex')) === revision });
      }
      if (indexEntries.length) meta.indexEntries = indexEntries;
      screenLogical(name); screenLogical(meta, parts);
      stores.push({ name, records: rows, metadata: meta });
    }
    for (const [name, sid] of d.names) requireSource(d.stores.has(sid) && utf16(d.stores.get(sid)!.fields.get(0)!, true) === name, 'Unresolved store-name mapping.');
    screenLogical(metadata, parts);
    output.push({ name: link.name, version: Number(version), stores: stores.sort((a, b) => a.name < b.name ? -1 : 1), metadata });
  }
  return { origin, identifier, databases: output.sort((a, b) => a.name < b.name ? -1 : 1), ownedBlobFiles };
}
