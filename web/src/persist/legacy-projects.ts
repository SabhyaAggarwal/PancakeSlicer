import { documentText, sha256 } from './wire';
import type { DesktopCapture } from './desktop-legacy';
export interface LegacyStore { name: string; records: { key: IDBValidKey; value: unknown }[] }
export interface LegacyDatabase { name: string; version: number; stores: LegacyStore[] }
export interface LegacyProjectInventory {
  databases: LegacyDatabase[];
  recovery: string | null;
  lineage: string | null;
  owner: string | null;
  preferences?: Record<string, string>;
  desktop?: DesktopCapture;
}
const profileInputs = new Set(['orca.userPresets', 'orca.profileOverlays.v1', 'orca.profileBases.v1', 'orca.profileDefaults.v1']);
export function captureLegacyPreferences(): Record<string, string> {
  if (typeof localStorage === 'undefined' || !Number.isSafeInteger(localStorage.length)) throw new Error('Cannot enumerate saved browser preferences. Adoption is incomplete.');
  const result: Record<string, string> = {};
  for (let index = 0; index < localStorage.length; index++) {
    const key = localStorage.key(index);
    if (!key || !key.startsWith('orca.') || profileInputs.has(key) || /token|password|secret|credential|api.?key/i.test(key)
      || key === 'orca.project-change' || key === 'orca.project-draft-mutation') continue;
    const value = localStorage.getItem(key);
    if (value === null) throw new Error('Browser preferences changed during capture. Retry adoption.');
    result[key] = value;
  }
  return Object.fromEntries(Object.entries(result).sort(([a], [b]) => a.localeCompare(b)));
}
async function openExisting(name: string): Promise<IDBDatabase | null> {
  if (typeof indexedDB === 'undefined' || !indexedDB.databases) throw new Error('Cannot enumerate legacy project storage. Adoption is incomplete.');
  if (!(await indexedDB.databases()).some((database) => database.name === name)) return null;
  return new Promise((resolve, reject) => {
    const read = indexedDB.open(name);
    let absent = false, abandoned = false;
    read.onupgradeneeded = () => { absent = true; read.transaction!.abort(); };
    read.onsuccess = () => { if (abandoned) read.result.close(); else resolve(read.result); };
    read.onerror = () => absent ? resolve(null) : reject(read.error);
    read.onblocked = () => { abandoned = true; reject(new Error('Close older Orca windows before adopting project storage.')); };
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
  if (typeof window !== 'undefined' && window.orcaDesktop && !window.orcaDesktop.legacyProjects)
    throw new Error('This desktop host cannot export the complete legacy project inventory. Update the host before adopting; existing files are retained.');
  const host = typeof window === 'undefined' ? undefined : window.orcaDesktop?.legacyProjects;
  const desktop = host ? await (await import('./desktop-legacy')).captureDesktopProjects(host) : undefined;
  const databases = await Promise.all([
    captureDatabase('orca-web', ['sessions', 'blobs', 'trash', 'aliases', 'quarantine', 'tombstones']),
    captureDatabase('orca-web-drafts', ['drafts', 'metadata']),
  ]);
  const read = (key: string) => typeof sessionStorage === 'undefined' ? null : sessionStorage.getItem(key);
  return { databases: [...databases.filter((db): db is LegacyDatabase => db !== null), ...(desktop ? [desktop.database] : [])], ...(desktop ? { desktop } : {}),
    recovery: read('orca.fsm.recovery.v1'), lineage: read('orca.project-draft-lineage'), owner: read('orca.project-draft-owner'), preferences: captureLegacyPreferences() };
}
export const hasLegacyProjects = (inventory: LegacyProjectInventory) => !!inventory.desktop || inventory.databases.some((db) => db.stores.some((store) => store.records.length)) || inventory.recovery !== null || Object.keys(inventory.preferences ?? {}).length > 0;

export interface AdoptionPart { key: string; blob: Blob; sha256: string; name?: string; lastModified?: number }
export async function legacyProjectEnvelope(inventory: LegacyProjectInventory) {
  const parts: AdoptionPart[] = [];
  const partsByBlob = new WeakMap<Blob, AdoptionPart>();
  async function pack(value: unknown): Promise<unknown> {
    if (value instanceof Blob) {
      const hash = await sha256(await value.arrayBuffer());
      let part = parts.find((part) => part.sha256 === hash);
      if (!part) {
        part = { key: `blob-${parts.length}`, blob: value, sha256: hash };
        parts.push(part);
      }
      partsByBlob.set(value, part);
      return { $orcaBlob: part.key, sha256: part.sha256, type: value.type, size: value.size,
        ...(typeof File !== 'undefined' && value instanceof File ? { name: value.name, lastModified: value.lastModified } : {}) };
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
  return { inventory, manifest, digest, parts, partsByBlob };
}
