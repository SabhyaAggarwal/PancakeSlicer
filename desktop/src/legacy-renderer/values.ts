import { createHash } from 'node:crypto';
import type { RendererBinaryKind, RendererDecodedValue, RendererPartReference } from '../legacy-renderer';
import { Cursor, Incomplete, LIMITS, requireSource } from './leveldb';

export function utf8(bytes: Buffer): string {
  requireSource(bytes.length <= LIMITS.text);
  return new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes);
}
export function utf16(bytes: Buffer, bigEndian = false): string {
  requireSource(bytes.length % 2 === 0 && bytes.length <= LIMITS.text);
  let text = '';
  for (let start = 0; start < bytes.length; start += 8192) {
    const units: number[] = [];
    for (let at = start; at < Math.min(start + 8192, bytes.length); at += 2)
      units.push(bigEndian ? bytes.readUInt16BE(at) : bytes.readUInt16LE(at));
    text += String.fromCharCode(...units);
  }
  return text;
}
function finite(n: number) { requireSource(Number.isFinite(n) && !Object.is(n, -0), 'Unsupported scalar number.'); return n; }

export class Parts {
  readonly bytes = new Map<string, Buffer>();
  private size = 0;
  constructor(readonly origin = '') {}
  add(bytes: Buffer, kind: Exclude<RendererBinaryKind, 'Blob' | 'File'>): RendererPartReference {
    return this.keep(bytes, { kind });
  }
  external(bytes: Buffer, meta: { kind: 'Blob'; mimeType: string } | { kind: 'File'; mimeType: string; name: string; lastModified: number }): RendererPartReference {
    return this.keep(bytes, meta);
  }
  private keep(bytes: Buffer, meta: Omit<RendererPartReference, '$orcaRendererPart'>): RendererPartReference {
    this.size += bytes.length; requireSource(this.size <= LIMITS.logical);
    const key = `v1:${createHash('sha256').update(this.origin).digest('hex')}:${this.bytes.size}:${createHash('sha256').update(bytes).digest('hex')}`;
    this.bytes.set(key, bytes);
    return { $orcaRendererPart: key, ...meta } as RendererPartReference;
  }
}

export function decodeV8(bytes: Buffer, parts: Parts, host?: (tag: number, cursor: Cursor) => RendererDecodedValue): RendererDecodedValue {
  const c = new Cursor(bytes); requireSource(c.byte() === 255);
  const version = c.number(); requireSource(version === 15 || version === 16, 'Unsupported V8 version.');
  let nodes = 0;
  const next = () => { while (c.bytes[c.at] === 0) c.at++; return c.bytes[c.at]; };
  function value(depth: number): RendererDecodedValue {
    requireSource(depth <= LIMITS.depth && ++nodes <= LIMITS.records);
    next(); const tag = c.byte();
    if (tag === 48) return null;
    if (tag === 84 || tag === 70) return tag === 84;
    if (tag === 73) { const n = c.varint(32); return Number((n >> 1n) ^ -(n & 1n)); }
    if (tag === 85) return Number(c.varint(32));
    if (tag === 78) return finite(c.take(8).readDoubleLE());
    if (tag === 83) return utf8(c.sized());
    if (tag === 34) return c.sized().toString('latin1');
    if (tag === 99) return utf16(c.sized());
    if (tag === 68) {
      const date = finite(c.take(8).readDoubleLE());
      requireSource(Number.isInteger(date) && Math.abs(date) <= 8.64e15, 'Unsupported Date value.');
      return { $orcaDate: date };
    }
    if (tag === 111) {
      const result: { [key: string]: RendererDecodedValue } = Object.create(null);
      let count = 0;
      while (next() !== 123) {
        const rawKey = value(depth + 1);
        const key = typeof rawKey === 'number' && Number.isInteger(rawKey) && rawKey >= 0 && rawKey < 0xffffffff ? String(rawKey) : rawKey;
        requireSource(typeof key === 'string' && !Object.hasOwn(result, key)
          && !key.startsWith('$orca'), 'Ambiguous source object key.');
        result[key] = value(depth + 1); count++;
      }
      c.byte(); requireSource(c.number() === count); return result;
    }
    if (tag === 65) {
      const length = c.number(); requireSource(length <= LIMITS.records);
      const result: RendererDecodedValue[] = [];
      for (let i = 0; i < length; i++) result.push(value(depth + 1));
      requireSource(next() === 36); c.byte(); requireSource(c.number() === 0 && c.number() === length);
      return result;
    }
    if (tag === 66) {
      const raw = c.sized();
      let kind: Exclude<RendererBinaryKind, 'Blob' | 'File'> = 'ArrayBuffer';
      if (next() === 86) {
        c.byte(); const subtype = c.number(), offset = c.number(), length = c.number(), flags = c.number();
        const types: Record<number, [Exclude<RendererBinaryKind, 'Blob' | 'File'>, number]> = {
          98: ['Int8Array', 1], 66: ['Uint8Array', 1], 67: ['Uint8ClampedArray', 1],
          119: ['Int16Array', 2], 87: ['Uint16Array', 2], 100: ['Int32Array', 4], 68: ['Uint32Array', 4],
          102: ['Float32Array', 4], 70: ['Float64Array', 8], 113: ['BigInt64Array', 8], 81: ['BigUint64Array', 8],
          63: ['DataView', 1],
        };
        const type = types[subtype];
        requireSource(type && offset === 0 && length === raw.length && length % type[1] === 0 && flags === 0,
          'Unsupported buffer view identity or flags.');
        kind = type[0];
      }
      return parts.add(raw, kind);
    }
    if (tag === 92 && host) return host(c.byte(), c);
    throw new Incomplete('unsupported_type', 'Unsupported V8/Blink value or reference semantics.');
  }
  const result = value(0); next(); c.done(); return result;
}

const secret = /password|passwd|secret|bearer|token|api[_. -]?key|access[_. -]?code|authorization|credential|print[_. -]?host/i;
const ownedKeys = /^(?:orca\.(?:userPresets|profile(?:Overlays|Bases|Defaults)\.v1|printer|vendor|project-change|developer_mode|enable_high_low_temp_mixed_printing|web\.(?:theme|view|camera|sidebar|railWidth|railObjects|nonplanar_view|sendPrinter|filamentsToUse|arrange|clone|recentCommands))|orca[-.]picker[.:].+)$/;
export function screenStorage(key: string, raw: string) {
  if (!ownedKeys.test(key) || secret.test(key)) throw new Incomplete('sensitive_source', 'Unestablished application ownership or excluded credential key.');
  requireSource(raw.length <= LIMITS.text); screenLogical(raw);
}
export function screenLogical(value: unknown, parts?: Parts) {
  let nodes = 0;
  const visit = (item: unknown, depth: number): void => {
    requireSource(++nodes <= LIMITS.records && depth <= LIMITS.depth);
    if (typeof item === 'string') {
      if (secret.test(item) || /[a-z][a-z0-9+.-]*:\/\/[^\s/]*@|-----BEGIN |\b[A-Za-z0-9+/=_-]{80,}\b/i.test(item)
        || item.length > 16384) throw new Incomplete('sensitive_source', 'Opaque or potentially credential-bearing logical value.');
      if (/^\s*[\[{]/.test(item)) {
        try { JSON.parse(item); } catch { throw new Incomplete('sensitive_source', 'Unresolved structured logical value.'); }
        for (let at = 0; at < item.length; at++) if (item[at] === '"') {
          const start = at++;
          for (; at < item.length && item[at] !== '"'; at++) if (item[at] === '\\') at++;
          requireSource(at < item.length);
          visit(JSON.parse(item.slice(start, at + 1)), depth + 1);
        }
      }
    } else if (item && typeof item === 'object' && Object.hasOwn(item, '$orcaRendererPart')) {
      const ref = item as RendererPartReference;
      const bytes = parts?.bytes.get(ref.$orcaRendererPart);
      requireSource(bytes, 'Unowned logical binary part.');
      screenBinary(bytes, ref.kind === 'Blob' || ref.kind === 'File' ? ref : undefined);
    } else if (Array.isArray(item)) for (const child of item) visit(child, depth + 1);
    else if (item && typeof item === 'object') for (const [name, child] of Object.entries(item)) {
      if (secret.test(name)) throw new Incomplete('sensitive_source', 'Excluded nested credential field.');
      visit(child, depth + 1);
    }
  };
  visit(value, 0);
}

export function screenBinary(bytes: Buffer, file?: { kind: 'Blob' | 'File'; mimeType: string; name?: string }) {
  for (let at = 0; at < bytes.length; at += 65536) {
    const chunk = bytes.subarray(Math.max(0, at - 64), Math.min(bytes.length, at + 65536));
    const texts = [chunk.toString('latin1')], even = chunk.subarray(0, chunk.length - chunk.length % 2);
    texts.push(utf16(even), utf16(even, true));
    const odd = chunk.subarray(1, 1 + Math.max(0, chunk.length - 1 - (chunk.length - 1) % 2));
    texts.push(utf16(odd), utf16(odd, true));
    if (texts.some((text) => secret.test(text) || /-----BEGIN |(?:https?|ftp):\/\//i.test(text)))
      throw new Incomplete('sensitive_source', 'Potentially credential-bearing binary part.');
  }
  if (file) {
    const latin = bytes.toString('latin1');
    screenLogical(file.mimeType); if (file.name !== undefined) screenLogical(file.name);
    requireSource(!file.name?.includes('/') && !file.name?.includes('\\'), 'Unsupported File path metadata.');
    const png = bytes.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))
      && !/tEXt|zTXt|iTXt/.test(latin);
    const jpeg = bytes[0] === 255 && bytes[1] === 216 && bytes[bytes.length - 2] === 255 && bytes[bytes.length - 1] === 217
      && !/Exif|http:|xmp|ICC_PROFILE/i.test(latin);
    const stl = bytes.length >= 84 && 84 + 50 * bytes.readUInt32LE(80) === bytes.length;
    const asciiSTL = bytes.length <= LIMITS.text && /^solid[\s\S]*endsolid[^\r\n]*\s*$/.test(latin);
    requireSource(png && file.mimeType === 'image/png' || jpeg && file.mimeType === 'image/jpeg'
      || (stl || asciiSTL) && ['', 'application/octet-stream', 'model/stl', 'application/sla'].includes(file.mimeType),
    'Opaque or unsupported external application file.');
  }
}
