import { documentText } from './wire';
export interface LegacyStore { name: string; records: { key: IDBValidKey; value: unknown }[] }
export interface LegacyDatabase { name: string; version: number; stores: LegacyStore[] }
export interface LegacyProjectInventory {
  databases: LegacyDatabase[];
  recovery: string | null;
  lineage: string | null;
  owner: string | null;
}
async function openExisting(name: string): Promise<IDBDatabase | null> {
  if (typeof indexedDB === 'undefined') return null;
  return new Promise((resolve, reject) => {
    const read = indexedDB.open(name);
    let absent = false;
    read.onupgradeneeded = () => { absent = true; read.transaction!.abort(); };
    read.onsuccess = () => resolve(read.result);
    read.onerror = () => absent ? resolve(null) : reject(read.error);
    read.onblocked = () => reject(new Error('Close older Orca windows before adopting project storage.'));
  });
}
async function captureDatabase(name: string, wanted: readonly string[]): Promise<LegacyDatabase | null> {
  const db = await openExisting(name);
  if (!db) return null;
  try {
    const names = wanted.filter((store) => db.objectStoreNames.contains(store));
    if (!names.length) return { name, version: db.version, stores: [] };
    return await new Promise<LegacyDatabase>((resolve, reject) => {
      const tx = db.transaction(names, 'readonly'), stores: LegacyStore[] = [];
      for (const name of names) {
        const records: LegacyStore['records'] = [], read = tx.objectStore(name).openCursor();
        stores.push({ name, records });
        read.onsuccess = () => { const cursor = read.result; if (cursor) { records.push({ key: cursor.key, value: cursor.value }); cursor.continue(); } };
      }
      tx.oncomplete = () => resolve({ name, version: db.version, stores });
      tx.onerror = tx.onabort = () => reject(tx.error ?? new Error('Cannot read legacy project inventory'));
    });
  } finally { db.close(); }
}
export async function captureLegacyProjects(): Promise<LegacyProjectInventory> {
  const databases = await Promise.all([
    captureDatabase('orca-web', ['sessions', 'blobs', 'trash', 'aliases', 'quarantine', 'tombstones']),
    captureDatabase('orca-web-drafts', ['drafts', 'metadata']),
  ]);
  const read = (key: string) => typeof sessionStorage === 'undefined' ? null : sessionStorage.getItem(key);
  return { databases: databases.filter((db): db is LegacyDatabase => db !== null),
    recovery: read('orca.fsm.recovery.v1'), lineage: read('orca.project-draft-lineage'), owner: read('orca.project-draft-owner') };
}
export const hasLegacyProjects = (inventory: LegacyProjectInventory) => inventory.databases.some((db) => db.stores.some((store) => store.records.length)) || inventory.recovery !== null;

export interface AdoptionPart { key: string; blob: Blob; sha256: string; name?: string; lastModified?: number }
export const sha256 = async (bytes: BufferSource) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map((byte) => byte.toString(16).padStart(2, '0')).join('');
export async function legacyProjectEnvelope(inventory: LegacyProjectInventory) {
  const parts: AdoptionPart[] = [];
  async function pack(value: unknown): Promise<unknown> {
    if (value instanceof Blob) {
      const part: AdoptionPart = { key: `blob-${parts.length}`, blob: value, sha256: await sha256(await value.arrayBuffer()),
        ...(typeof File !== 'undefined' && value instanceof File ? { name: value.name, lastModified: value.lastModified } : {}) };
      parts.push(part);
      return { $orcaBlob: part.key, sha256: part.sha256, type: value.type, size: value.size,
        ...(part.name === undefined ? {} : { name: part.name, lastModified: part.lastModified }) };
    }
    if (value instanceof Date) return { $orcaDate: value.toISOString() };
    if (ArrayBuffer.isView(value) || value instanceof ArrayBuffer) return value;
    if (Array.isArray(value)) { const result = []; for (const item of value) result.push(await pack(item)); return result; }
    if (value && typeof value === 'object') {
      if (Object.getPrototypeOf(value) !== Object.prototype && Object.getPrototypeOf(value) !== null) throw new Error('Unsupported legacy project value; keep the source for recovery.');
      const result: Record<string, unknown> = {};
      for (const [key, item] of Object.entries(value)) result[key] = await pack(item);
      return result;
    }
    if (typeof value === 'undefined') return { $orcaUndefined: true };
    if (typeof value === 'bigint') return { $orcaBigInt: String(value) };
    return value;
  }
  const manifest = documentText({ version: 1, inventory: await pack(inventory) });
  const digest = await sha256(new TextEncoder().encode(manifest));
  return { inventory, manifest, digest, parts };
}
