const tag = '$orcaBinary';
const arrays = { Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array,
  Float32Array, Float64Array, BigInt64Array, BigUint64Array };
function base64(bytes: Uint8Array): string {
  const chunks: string[] = [];
  for (let at = 0; at < bytes.length; at += 16384) chunks.push(String.fromCharCode(...bytes.subarray(at, at + 16384)));
  return btoa(chunks.join(''));
}
export function documentText(value: unknown): string {
  return JSON.stringify(value, (_key, item: unknown) => {
    if (item instanceof ArrayBuffer) return { [tag]: 'ArrayBuffer', data: base64(new Uint8Array(item)) };
    if (ArrayBuffer.isView(item)) {
      const type = item instanceof DataView ? 'DataView' : item.constructor.name;
      if (type !== 'DataView' && !Object.hasOwn(arrays, type)) throw new Error(`Unsupported document array: ${type}`);
      return { [tag]: type, data: base64(new Uint8Array(item.buffer, item.byteOffset, item.byteLength)) };
    }
    if (typeof item === 'number' && !Number.isFinite(item)) throw new Error('Project contains a non-finite number');
    return item;
  });
}
export function parseDocument<T>(text: string): T {
  return JSON.parse(text, (_key, item: unknown) => {
    if (!item || typeof item !== 'object' || !Object.hasOwn(item, tag)) return item;
    const { [tag]: type, data } = item as Record<string, unknown>;
    if (typeof type !== 'string' || typeof data !== 'string') throw new Error('Invalid binary document field');
    const bytes = Uint8Array.from(atob(data), (c) => c.charCodeAt(0));
    if (type === 'ArrayBuffer') return bytes.buffer;
    if (type === 'DataView') return new DataView(bytes.buffer);
    if (!Object.hasOwn(arrays, type)) throw new Error(`Unknown document array: ${type}`);
    const ArrayType = arrays[type as keyof typeof arrays];
    if (bytes.byteLength % ArrayType.BYTES_PER_ELEMENT) throw new Error('Truncated binary document field');
    return new ArrayType(bytes.buffer);
  }) as T;
}
