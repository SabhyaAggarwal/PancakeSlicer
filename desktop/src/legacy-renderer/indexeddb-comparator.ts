import { Cursor, LIMITS, requireSource, type UserComparator } from './leveldb';

type UserKey = { tag: number; value?: number | Buffer | UserKey[] };
export function prefix(c: Cursor) {
  const first = c.byte();
  const integer = (n: number) => {
    let value = 0n; const bytes = c.take(n);
    for (let i = 0; i < n; i++) value |= BigInt(bytes[i]) << BigInt(i * 8);
    requireSource(value < (1n << 63n)); return value;
  };
  return { database: integer((first >>> 5) + 1), store: integer(((first >>> 2) & 7) + 1), index: integer((first & 3) + 1) };
}
export function stringBytes(c: Cursor): Buffer { const n = c.number(); requireSource(n <= LIMITS.text / 2); return c.take(n * 2); }
export function encodedKey(c: Cursor, depth = 0, budget = { nodes: 0 }): UserKey {
  requireSource(depth <= LIMITS.depth && ++budget.nodes <= LIMITS.records);
  const tag = c.byte();
  if (tag === 0 || tag === 5) return { tag };
  if (tag === 1) return { tag, value: stringBytes(c) };
  if (tag === 6) return { tag, value: c.sized() };
  if (tag === 2 || tag === 3) {
    const value = c.take(8).readDoubleLE(); requireSource(!Number.isNaN(value)); return { tag, value };
  }
  requireSource(tag === 4);
  const n = c.number(); requireSource(n <= LIMITS.records);
  const value: UserKey[] = []; for (let i = 0; i < n; i++) value.push(encodedKey(c, depth + 1, budget));
  return { tag, value };
}
const cmpNumber = (a: number | bigint, b: number | bigint) => a === b ? 0 : a < b ? -1 : 1;
function compareKey(a: UserKey, b: UserKey): number {
  const rank = [6, 3, 2, 1, 5, 0, 4];
  if (a.tag !== b.tag) return rank[a.tag] - rank[b.tag];
  if (a.tag === 0 || a.tag === 5) return 0;
  if (a.tag === 1 || a.tag === 6) return Buffer.compare(a.value as Buffer, b.value as Buffer);
  if (a.tag === 2 || a.tag === 3) return cmpNumber(a.value as number, b.value as number);
  const x = a.value as UserKey[], y = b.value as UserKey[];
  for (let i = 0; i < Math.min(x.length, y.length); i++) { const order = compareKey(x[i], y[i]); if (order) return order; }
  return x.length - y.length;
}
type Atom = bigint | number | Buffer | UserKey;
function atoms(bytes: Buffer): Atom[] {
  const c = new Cursor(bytes), p = prefix(c), out: Atom[] = [p.database, p.store, p.index];
  const identifier = () => { const n = c.varint(); requireSource(n < (1n << 63n)); return n; };
  if (p.database === 0n || p.store === 0n) {
    requireSource(p.database !== 0n || p.store === 0n && p.index === 0n);
    requireSource(p.store !== 0n || p.index === 0n);
    const tag = c.byte(); out.push(tag);
    if (p.database === 0n) {
      if (tag <= 6) {  }
      else if (tag === 50) out.push(c.take(bytes.length - c.at));
      else if (tag === 100) out.push(identifier());
      else { requireSource(tag === 201); out.push(stringBytes(c), stringBytes(c)); }
    } else if (tag <= 5) {  }
    else if (tag === 50) out.push(identifier(), c.byte());
    else if (tag === 100) out.push(identifier(), identifier(), c.byte());
    else if (tag === 150) out.push(identifier());
    else if (tag === 151) out.push(identifier(), identifier());
    else if (tag === 200) out.push(stringBytes(c));
    else { requireSource(tag === 201); out.push(identifier(), stringBytes(c)); }
  } else {
    requireSource(p.index === 1n || p.index === 2n || p.index === 3n || p.index >= 30n);
    out.push(encodedKey(c));
    if (p.index >= 30n) {
      const sequence = c.at < bytes.length ? identifier() : -1n;
      if (c.at < bytes.length) out.push(1, encodedKey(c), sequence);
      else out.push(0);
    }
  }
  c.done(); return out;
}
export const idbComparator: UserComparator = {
  name: 'idb_cmp1',
  compare(a, b) {
    const x = atoms(a), y = atoms(b);
    for (let i = 0; i < Math.min(x.length, y.length); i++) {
      const p = x[i], q = y[i];
      const order = Buffer.isBuffer(p) && Buffer.isBuffer(q) ? Buffer.compare(p, q)
        : typeof p === 'object' && typeof q === 'object' ? compareKey(p as UserKey, q as UserKey)
        : (typeof p === 'number' || typeof p === 'bigint') && (typeof q === 'number' || typeof q === 'bigint') ? cmpNumber(p, q)
        : (() => { throw new Error('Incompatible IndexedDB comparator key.'); })();
      if (order) return order;
    }
    return x.length - y.length;
  },
};
