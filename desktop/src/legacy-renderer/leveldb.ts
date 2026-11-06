


import { unsnappy } from './snappy';

export class Incomplete extends Error {
  constructor(readonly code: 'unsupported_layout' | 'unsupported_type' | 'source_changed' | 'sensitive_source'
    | 'source_locked' | 'missing_part' | 'invalid_origin' | 'unverified_absence', message: string) { super(message); }
}
export function requireSource(ok: unknown, message = 'Unsupported or damaged source encoding.'): asserts ok {
  if (!ok) throw new Incomplete('unsupported_type', message);
}
export const LIMITS = Object.freeze({ bytes: 64 << 20, file: 16 << 20, block: 4 << 20,
  records: 100_000, files: 4096, text: 2 << 20, depth: 48, logical: 8 << 20 });
export class Budget {
  private records = 0;
  private expanded = 0;
  record() { requireSource(++this.records <= LIMITS.records, 'Record budget exceeded.'); }
  expand(size: number) { this.expanded += size; requireSource(this.expanded <= LIMITS.bytes, 'Decode budget exceeded.'); }
}
export class Cursor {
  at = 0;
  constructor(readonly bytes: Buffer) {}
  take(length: number): Buffer {
    requireSource(Number.isSafeInteger(length) && length >= 0 && length <= this.bytes.length - this.at);
    const value = this.bytes.subarray(this.at, this.at + length); this.at += length; return value;
  }
  byte() { return this.take(1)[0]; }
  varint(bits = 64): bigint {
    let value = 0n;
    for (let shift = 0; shift < bits; shift += 7) {
      const byte = this.byte(); value |= BigInt(byte & 127) << BigInt(shift);
      if (!(byte & 128)) {
        requireSource(value < (1n << BigInt(bits)) && (shift === 0 || byte !== 0));
        return value;
      }
    }
    throw new Incomplete('unsupported_type', 'Invalid source integer.');
  }
  number() { const n = this.varint(); requireSource(n <= BigInt(Number.MAX_SAFE_INTEGER)); return Number(n); }
  sized() { const n = this.number(); requireSource(n <= LIMITS.text); return this.take(n); }
  done() { requireSource(this.at === this.bytes.length, 'Unaccounted source bytes.'); }
}

function crc32c(bytes: Buffer, previous = 0): number {
  let crc = (previous ^ 0xffffffff) >>> 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (crc & 1 ? 0x82f63b78 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function checkCRC(bytes: Buffer, stored: number, prefix?: number, suffix?: number) {
  let crc = prefix === undefined ? 0 : crc32c(Buffer.from([prefix]));
  crc = crc32c(bytes, crc);
  if (suffix !== undefined) crc = crc32c(Buffer.from([suffix]), crc);
  requireSource((((crc >>> 15) | (crc << 17)) + 0xa282ead8) >>> 0 === stored, 'Source checksum mismatch.');
}


function logRecords(file: Buffer, budget: Budget): Buffer[] {
  const records: Buffer[] = [];
  let fragments: Buffer[] | null = null, fragmentBytes = 0;
  for (let start = 0; start < file.length; start += 32768) {
    const block = file.subarray(start, Math.min(start + 32768, file.length));
    let at = 0;
    while (at < block.length) {
      const rest = block.subarray(at);
      if (rest.length < 7 || rest.subarray(0, 7).every((v) => v === 0)) {
        requireSource(rest.every((v) => v === 0), 'Ambiguous log padding.');
        requireSource(rest.length < 7 || fragments === null, 'Fragment interrupted by log padding.');
        break;
      }
      const crc = block.readUInt32LE(at), size = block.readUInt16LE(at + 4), type = block[at + 6];
      at += 7;
      requireSource(type >= 1 && type <= 4 && size <= block.length - at);
      const payload = block.subarray(at, at + size); at += size;
      checkCRC(payload, crc, type);
      if (type === 1) {
        requireSource(fragments === null); budget.record(); records.push(payload);
      } else if (type === 2) {
        requireSource(fragments === null); fragments = [payload]; fragmentBytes = size;
      } else {
        requireSource(fragments !== null); fragments.push(payload); fragmentBytes += size;
        requireSource(fragmentBytes <= LIMITS.file);
        if (type === 4) { budget.record(); budget.expand(fragmentBytes); records.push(Buffer.concat(fragments)); fragments = null; }
      }
    }
  }
  requireSource(fragments === null, 'Unfinished log fragment.');
  return records;
}

interface Internal { key: Buffer; value: Buffer; sequence: bigint; deleted: boolean; tag: bigint }
interface FileMeta { number: bigint; size: number; smallest: Buffer; largest: Buffer }
function internal(key: Buffer, value: Buffer): Internal {
  requireSource(key.length >= 8);
  const tag = key.readBigUInt64LE(key.length - 8), type = Number(tag & 255n);
  requireSource(type === 0 || type === 1);
  requireSource(type !== 0 || value.length === 0);
  return { key: key.subarray(0, -8), value, sequence: tag >> 8n, deleted: type === 0, tag };
}
export interface UserComparator { name: string; compare(a: Buffer, b: Buffer): number }
const bytewise: UserComparator = { name: 'leveldb.BytewiseComparator', compare: Buffer.compare };
function compareInternal(a: Buffer, b: Buffer, comparator: UserComparator): number {
  requireSource(a.length >= 8 && b.length >= 8);
  const order = comparator.compare(a.subarray(0, -8), b.subarray(0, -8));
  const x = a.readBigUInt64LE(a.length - 8), y = b.readBigUInt64LE(b.length - 8);
  return order || (x === y ? 0 : x > y ? -1 : 1);
}
function entries(block: Buffer, budget: Budget): { key: Buffer; value: Buffer }[] {
  requireSource(block.length >= 8);
  const count = block.readUInt32LE(block.length - 4), boundary = block.length - 4 - count * 4;
  requireSource(count >= 1 && count <= LIMITS.records && boundary >= 0);
  const restarts = new Set<number>(); let previous = -1;
  for (let i = 0; i < count; i++) {
    const offset = block.readUInt32LE(boundary + i * 4);
    requireSource((i !== 0 || offset === 0) && offset > previous && (offset < boundary || boundary === 0 && count === 1));
    restarts.add(offset); previous = offset;
  }
  const cursor = new Cursor(block.subarray(0, boundary)), result: { key: Buffer; value: Buffer }[] = [];
  let key: Buffer = Buffer.alloc(0);
  while (cursor.at < boundary) {
    budget.record(); const offset = cursor.at;
    const shared = cursor.number(), added = cursor.number(), length = cursor.number();
    requireSource(shared <= key.length && shared + added <= LIMITS.text && (!restarts.has(offset) || shared === 0));
    restarts.delete(offset);
    budget.expand(shared + added);
    key = Buffer.concat([key.subarray(0, shared), cursor.take(added)]);
    result.push({ key, value: cursor.take(length) });
  }
  requireSource(boundary === 0 || restarts.size === 0, 'Invalid block restart boundary.');
  return result;
}
interface Handle { offset: number; length: number }
const handle = (cursor: Cursor): Handle => ({ offset: cursor.number(), length: cursor.number() });
function table(file: Buffer, meta: FileMeta, budget: Budget, comparator: UserComparator): Internal[] {
  const compare = (a: Buffer, b: Buffer) => compareInternal(a, b, comparator);
  requireSource(file.length === meta.size && file.length >= 48 && file.readBigUInt64LE(file.length - 8) === 0xdb4775248b80fb57n);
  const footer = new Cursor(file.subarray(file.length - 48, file.length - 8));
  const metaHandle = handle(footer), indexHandle = handle(footer);
  requireSource(footer.take(40 - footer.at).every((v) => v === 0));
  const spans: { start: number; end: number }[] = [];
  const readBlock = (h: Handle) => {
    requireSource(h.length <= LIMITS.block && h.offset >= 0 && h.offset + h.length + 5 <= file.length - 48);
    const payload = file.subarray(h.offset, h.offset + h.length), kind = file[h.offset + h.length];
    requireSource(kind === 0 || kind === 1, 'Unsupported table compression.');
    checkCRC(payload, file.readUInt32LE(h.offset + h.length + 1), undefined, kind);
    spans.push({ start: h.offset, end: h.offset + h.length + 5 });
    const result = kind === 0 ? payload : unsnappy(payload, LIMITS.block); budget.expand(result.length);
    return result;
  };
  const metaEntries = entries(readBlock(metaHandle), budget);
  let priorMeta: Buffer | undefined;
  for (const entry of metaEntries) {
    requireSource((!priorMeta || Buffer.compare(priorMeta, entry.key) < 0)
      && entry.key.toString('ascii') === 'filter.leveldb.BuiltinBloomFilter2', 'Unknown table metadata.');
    priorMeta = entry.key;
    const cursor = new Cursor(entry.value); const filterHandle = handle(cursor); cursor.done(); readBlock(filterHandle);
  }
  const index = entries(readBlock(indexHandle), budget), output: Internal[] = [];
  let priorKey: Buffer | undefined, priorSeparator: Buffer | undefined;
  for (const entry of index) {
    const cursor = new Cursor(entry.value), dataHandle = handle(cursor); cursor.done();
    const data = entries(readBlock(dataHandle), budget);
    requireSource(data.length > 0);
    if (priorSeparator) requireSource(compare(priorSeparator, data[0].key) < 0);
    for (const record of data) {
      requireSource(!priorKey || compare(priorKey, record.key) < 0, 'Unordered table keys.');
      priorKey = record.key; output.push(internal(record.key, record.value));
    }
    requireSource(compare(priorKey!, entry.key) <= 0);
    priorSeparator = entry.key;
  }
  requireSource(output.length > 0 && priorKey && compare(meta.smallest, dataKey(output[0])) === 0
    && compare(meta.largest, priorKey) === 0, 'Table manifest bounds differ.');
  spans.sort((a, b) => a.start - b.start);
  let end = 0;
  for (const span of spans) { requireSource(span.start === end, 'Unaccounted or overlapping table blocks.'); end = span.end; }
  requireSource(end === file.length - 48);
  return output;
}
function dataKey(record: Internal): Buffer {
  const tag = Buffer.alloc(8); tag.writeBigUInt64LE(record.tag); return Buffer.concat([record.key, tag]);
}

export interface CurrentRecord { key: Buffer; value: Buffer }

export async function currentView(names: readonly string[], read: (name: string) => Promise<Buffer>, budget: Budget, expected: UserComparator = bytewise): Promise<CurrentRecord[]> {
  const compare = (a: Buffer, b: Buffer) => compareInternal(a, b, expected);
  requireSource(names.includes('CURRENT'), 'Missing CURRENT selector.');
  const selected = await read('CURRENT');
  requireSource(/^MANIFEST-[0-9]{6,}\n$/.test(selected.toString('ascii')) && selected.every((v) => v < 128));
  const manifestName = selected.toString('ascii').slice(0, -1);
  requireSource(names.includes(manifestName), 'Missing selected manifest.');
  const live = new Map<string, { level: number; meta: FileMeta }>();
  let comparator = '', logNumber: bigint | null = null, prevLogNumber = 0n, nextFile: bigint | null = null, lastSequence = 0n;
  for (const edit of logRecords(await read(manifestName), budget)) {
    const cursor = new Cursor(edit), seen = new Set<number>();
    const removed: { level: number; number: bigint }[] = [], added: { level: number; meta: FileMeta }[] = [];
    while (cursor.at < edit.length) {
      const tag = cursor.number();
      if (![5, 6, 7].includes(tag)) { requireSource(!seen.has(tag)); seen.add(tag); }
      if (tag === 1) {
        const value = cursor.sized().toString('utf8'); requireSource(!comparator || value === comparator);
        comparator = value;
        requireSource(value === expected.name, 'Unsupported source comparator.');
      } else if (tag === 2) { const n = cursor.varint(); requireSource(logNumber === null || n >= logNumber); logNumber = n; }
      else if (tag === 3) { const n = cursor.varint(); requireSource(nextFile === null || n >= nextFile); nextFile = n; }
      else if (tag === 4) { const n = cursor.varint(); requireSource(n >= lastSequence && n < (1n << 56n)); lastSequence = n; }
      else if (tag === 9) prevLogNumber = cursor.varint();
      else if (tag === 5) { requireSource(cursor.number() <= 6); internal(cursor.sized(), Buffer.alloc(0)); }
      else if (tag === 6) { const level = cursor.number(); requireSource(level <= 6); removed.push({ level, number: cursor.varint() }); }
      else if (tag === 7) {
        const level = cursor.number(), number = cursor.varint(), size = cursor.number(), smallest = cursor.sized(), largest = cursor.sized();
        requireSource(level <= 6 && number > 0n && size <= LIMITS.file && compare(smallest, largest) <= 0);
        added.push({ level, meta: { number, size, smallest, largest } });
      } else throw new Incomplete('unsupported_type', 'Unknown manifest version-edit tag.');
    }
    for (const item of removed) {
      const key = item.number.toString();
      requireSource(!live.has(key) || live.get(key)!.level === item.level); live.delete(key);
    }
    for (const item of added) { const key = item.meta.number.toString(); requireSource(!live.has(key)); live.set(key, item); }
  }
  requireSource(comparator === expected.name && logNumber !== null && nextFile !== null && nextFile > 0n,
    'Incomplete manifest state.');
  requireSource(logNumber < nextFile && (prevLogNumber === 0n || prevLogNumber < logNumber));
  const files = new Map<string, string[]>();
  for (const name of names) if (/^[0-9]{6,}\.(?:ldb|sst|log)$/.test(name)) {
    const number = BigInt(name.split('.')[0]).toString(); files.set(number, [...files.get(number) ?? [], name]);
  }
  const all: Internal[] = [];
  for (let level = 1; level <= 6; level++) {
    const ordered = [...live.values()].filter((f) => f.level === level)
      .sort((a, b) => compare(a.meta.smallest, b.meta.smallest));
    for (let i = 1; i < ordered.length; i++) {
      requireSource(expected.compare(internal(ordered[i - 1].meta.largest, Buffer.alloc(0)).key,
        internal(ordered[i].meta.smallest, Buffer.alloc(0)).key) < 0, 'Overlapping nonzero-level tables.');
    }
  }
  for (const { meta } of live.values()) {
    requireSource(meta.number < nextFile);
    const matches = (files.get(meta.number.toString()) ?? []).filter((name) => !name.endsWith('.log'));
    requireSource(matches.length === 1, 'Missing or ambiguous live table.');
    const records = table(await read(matches[0]), meta, budget, expected);
    for (const record of records) all.push(record);
  }
  let previousBatch = -1n;
  const logs = names.filter((name) => /^[0-9]{6,}\.log$/.test(name)
    && (BigInt(name.split('.')[0]) >= logNumber! || BigInt(name.split('.')[0]) === prevLogNumber))
    .sort((a, b) => BigInt(a.split('.')[0]) < BigInt(b.split('.')[0]) ? -1 : 1);
  requireSource(!logs.length || logNumber > 0n);
  requireSource(logs.every((name) => BigInt(name.split('.')[0]) < nextFile));
  requireSource(logNumber === 0n || logs.some((name) => BigInt(name.split('.')[0]) === logNumber), 'Missing current WAL.');
  requireSource(prevLogNumber === 0n || logs.some((name) => BigInt(name.split('.')[0]) === prevLogNumber), 'Missing previous WAL.');
  for (const name of logs) for (const batch of logRecords(await read(name), budget)) {
    const cursor = new Cursor(batch), start = cursor.take(8).readBigUInt64LE(), count = cursor.take(4).readUInt32LE();
    requireSource(count > 0 && count <= LIMITS.records && start > previousBatch && start + BigInt(count) <= (1n << 56n));
    const staged: Internal[] = [];
    for (let i = 0; i < count; i++) {
      budget.record(); const type = cursor.byte(); requireSource(type === 0 || type === 1);
      const key = cursor.sized(), value = type ? cursor.sized() : Buffer.alloc(0), sequence = start + BigInt(i);
      staged.push({ key, value, sequence, deleted: type === 0, tag: (sequence << 8n) | BigInt(type) });
    }
    cursor.done(); previousBatch = start + BigInt(count) - 1n;
    for (const record of staged) all.push(record);
  }
  const sequences = new Map<bigint, Internal>();
  for (const record of all) {
    const old = sequences.get(record.sequence);
    requireSource(!old || old.tag === record.tag && old.key.equals(record.key) && old.value.equals(record.value), 'Conflicting source sequence.');
    sequences.set(record.sequence, record);
  }
  for (const record of all) expected.compare(record.key, record.key);
  all.sort((a, b) => expected.compare(a.key, b.key) || (a.sequence === b.sequence ? 0 : a.sequence > b.sequence ? -1 : 1));
  const output: CurrentRecord[] = [];
  let prior: Buffer | undefined;
  for (const record of all) {
    if (prior && expected.compare(prior, record.key) === 0) continue;
    prior = record.key;
    if (!record.deleted) output.push({ key: record.key, value: record.value });
  }
  return output;
}
