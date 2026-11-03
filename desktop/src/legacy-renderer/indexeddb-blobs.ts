import type { RendererDecodedValue, RendererPartReference } from '../legacy-renderer';
import { Cursor, Incomplete, LIMITS, requireSource } from './leveldb';
import { stringBytes } from './indexeddb-comparator';
import { decodeV8, Parts, screenBinary, utf16 } from './values';
import { unsnappy } from './snappy';

export interface ExternalObject {
  kind: 'Blob' | 'File'; mimeType: string; size: number; bytes: Buffer;
  blobNumber: string; sourceRef: string; lastModifiedMicros?: string;
  name?: string; lastModified?: number; used: boolean; wrapper: boolean; ref?: RendererPartReference;
}
export async function externalObjects(raw: Buffer, database: bigint, blobRoot: string,
  read: (relative: string) => Promise<Buffer>, ownedFiles: Set<string>): Promise<ExternalObject[]> {
  const c = new Cursor(raw), result: ExternalObject[] = [];
  while (c.at < raw.length) {
    requireSource(result.length < LIMITS.files);
    const type = c.byte(); requireSource(type === 0 || type === 1, 'Unsupported external object type.');
    const number = c.varint(); requireSource(number >= 2n && number < (1n << 63n));
    const mimeType = utf16(stringBytes(c), true), size = c.number(); requireSource(size <= LIMITS.file);
    let name: string | undefined, lastModified: number | undefined, lastModifiedMicros: string | undefined;
    if (type === 1) {
      name = utf16(stringBytes(c), true);
      const rawMicros = c.varint(); requireSource(rawMicros < (1n << 63n));
      lastModifiedMicros = rawMicros.toString();
      const micros = rawMicros - 11644473600000000n;
      requireSource(micros % 1000n === 0n && micros / 1000n >= BigInt(Number.MIN_SAFE_INTEGER)
        && micros / 1000n <= BigInt(Number.MAX_SAFE_INTEGER), 'Unrepresentable File timestamp.');
      lastModified = Number(micros / 1000n);
    }
    requireSource(mimeType === mimeType.toLowerCase() && /^[\x20-\x7e]*$/.test(mimeType), 'Unrepresentable MIME metadata.');
    const relative = `${blobRoot}/${database.toString(16)}/${Number((number >> 8n) & 255n).toString(16).padStart(2, '0')}/${number.toString(16)}`;
    const bytes = await read(relative); requireSource(bytes.length === size, 'External object length differs.');
    ownedFiles.add(relative);
    result.push({ kind: type === 0 ? 'Blob' : 'File', mimeType, size, bytes, blobNumber: number.toString(), sourceRef: relative,
      ...(name === undefined ? {} : { name, lastModified, lastModifiedMicros }), used: false, wrapper: false });
  }
  return result;
}
export function approveExternal(item: ExternalObject, parts: Parts): RendererPartReference {
  requireSource(!item.wrapper && item.mimeType !== 'application/vnd.blink-idb-value-wrapper', 'Opaque serialized value is not an application asset.');
  if (!item.ref) {
    screenBinary(item.bytes, item);
    item.ref = item.kind === 'Blob' ? parts.external(item.bytes, { kind: 'Blob', mimeType: item.mimeType })
      : parts.external(item.bytes, { kind: 'File', mimeType: item.mimeType, name: item.name!, lastModified: item.lastModified! });
  }
  item.used = true;
  return { ...item.ref };
}

export function blinkValue(raw: Buffer, objects: ExternalObject[], parts: Parts): RendererDecodedValue {
  let bytes = raw;
  if (bytes[0] === 255 && bytes[1] === 17 && bytes[2] === 1) {
    const c = new Cursor(bytes); c.take(3); const size = c.number(), index = c.number(); c.done();
    const wrapper = objects[index];
    requireSource(index === objects.length - 1 && wrapper && wrapper.kind === 'Blob' && wrapper.size === size
      && wrapper.mimeType === 'application/vnd.blink-idb-value-wrapper', 'Unresolved serialized-value wrapper.');
    wrapper.used = true; wrapper.wrapper = true; bytes = wrapper.bytes;
  }
  if (bytes[0] === 255 && bytes[1] === 17 && bytes[2] === 2) bytes = unsnappy(bytes.subarray(3), LIMITS.block);
  const c = new Cursor(bytes); requireSource(c.byte() === 255);
  const blink = c.number(); requireSource(blink >= 16 && blink <= 21, 'Unsupported Blink envelope.');
  let end = bytes.length;
  if (blink === 21) {
    requireSource(c.byte() === 254); const offset = c.take(8).readBigUInt64BE(), size = c.take(4).readUInt32BE();
    if (offset !== 0n || size !== 0) {
      requireSource(offset >= BigInt(c.at) && offset + BigInt(size) === BigInt(bytes.length));
      end = Number(offset);
      const trailer = new Cursor(bytes.subarray(end));
      requireSource(trailer.byte() === 160); const count = trailer.take(4).readUInt32BE();
      requireSource(count <= 2);
      const tags = [...trailer.take(count)]; requireSource(new Set(tags).size === tags.length && tags.every((tag) => tag === 105 || tag === 101), 'Unsupported Blink exposed interface.');
      trailer.done();
    }
  }
  requireSource(c.at < end);
  return decodeV8(bytes.subarray(c.at, end), parts, (tag, cursor) => {
    if (tag !== 105 && tag !== 101) throw new Incomplete('unsupported_type', 'Unsupported Blink host object.');
    const index = Number(cursor.varint(32)), item = objects[index];
    requireSource(item && !item.used && !item.wrapper && item.kind === (tag === 105 ? 'Blob' : 'File'), 'Missing or mismatched external object reference.');
    return approveExternal(item, parts);
  });
}
