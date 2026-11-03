import { documentText, sha256 } from './wire';
import type { DesktopCapture, DesktopRendererCapture } from './desktop-legacy';
export interface LegacyStore { name: string; records: { key: IDBValidKey; value: unknown }[]; metadata?: Record<string, unknown> }
export interface LegacyDatabase { name: string; version: number; stores: LegacyStore[]; origin?: string; metadata?: Record<string, unknown> }
export interface LegacyProjectInventory {
  databases: LegacyDatabase[];
  recovery: string | null;
  lineage: string | null;
  owner: string | null;
  preferences?: Record<string, string>;
  desktop?: DesktopCapture;
  renderer?: DesktopRendererCapture;
}
const profileInputs = new Set(['orca.userPresets', 'orca.profileOverlays.v1', 'orca.profileBases.v1', 'orca.profileDefaults.v1']);
const preferenceKey = (key: string) => key.startsWith('orca.') && !profileInputs.has(key) && !/token|password|secret|credential|api.?key/i.test(key)
  && key !== 'orca.project-change' && key !== 'orca.project-draft-mutation';
export function captureLegacyPreferences(): Record<string, string> {
  if (typeof localStorage === 'undefined' || !Number.isSafeInteger(localStorage.length)) throw new Error('Cannot enumerate saved browser preferences. Adoption is incomplete.');
  const result: Record<string, string> = {};
  for (let index = 0; index < localStorage.length; index++) {
    const key = localStorage.key(index);
    if (!key || !preferenceKey(key)) continue;
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
async function captureDatabase(name: string): Promise<LegacyDatabase | null> {
  const db = await openExisting(name);
  if (!db) return null;
  try {
    const names = Array.from(db.objectStoreNames).sort();
    if (!names.length) return { name, version: db.version, stores: [] };
    return await new Promise<LegacyDatabase>((resolve, reject) => {
      const tx = db.transaction(names, 'readonly'), stores: LegacyStore[] = [];
      db.onversionchange = () => {
        reject(new Error('Legacy project storage schema changed during capture. Retry adoption.'));
        try { tx.abort(); } catch {  }
        db.close();
      };
      for (const name of names) {
        const records: LegacyStore['records'] = [], read = tx.objectStore(name).openCursor();
        stores.push({ name, records });
        read.onsuccess = () => { const cursor = read.result; if (cursor) { records.push({ key: cursor.key, value: cursor.value }); cursor.continue(); } };
        read.onerror = () => reject(read.error ?? new Error(`Cannot read legacy project store: ${name}`));
      }
      tx.oncomplete = () => resolve({ name, version: db.version, stores });
      tx.onerror = tx.onabort = () => reject(tx.error ?? new Error('Cannot read legacy project inventory'));
    });
  } finally { db.close(); }
}
export async function captureLegacyProjects(): Promise<LegacyProjectInventory> {
  const host = typeof window === 'undefined' ? undefined : window.orcaDesktop;
  if (host) {
    const source = await import('./desktop-legacy');
    if (!host.legacyProjects || !host.legacyRendererStorage)
      throw source.desktopLegacyIncomplete();
    const renderer = await source.captureDesktopRenderer(host.legacyRendererStorage);
    const desktop = await source.captureDesktopProjects(host.legacyProjects);
    await source.verifyDesktopRendererCapture(host.legacyRendererStorage, renderer);
    const databases = renderer.origins.flatMap((origin) => origin.databases.filter((db) => db.name === 'orca-web' || db.name === 'orca-web-drafts')
      .map((db) => ({ ...db, origin: origin.origin })));
    const preferences: Record<string, string> = Object.create(null), recoveries = new Set<string>(), owners = new Set<string>();
    for (const origin of renderer.origins) {
      for (const { key, value } of origin.localStorage) if (preferenceKey(key)) {
        if (Object.hasOwn(preferences, key) && preferences[key] !== value)
          throw Object.assign(new Error('Historical saved preferences differ across origins. Resolve the retained sources explicitly before adoption.'), { name: 'ProjectConflictError' });
        preferences[key] = value;
      }
      for (const namespace of origin.sessionStorageNamespaces) for (const { key, value } of namespace.values) {
        if (key === 'orca.fsm.recovery.v1') {
          let recovery: unknown;
          try { recovery = JSON.parse(value); } catch { throw new Error('Historical recovery data cannot be decoded. Original namespaces are retained.'); }
          if (!recovery || typeof recovery !== 'object' || Array.isArray(recovery) || (recovery as {version?: unknown}).version !== 1) throw new Error('Historical recovery data is incomplete. Original namespaces are retained.');
          recoveries.add(value);
        }
        if (key === 'orca.project-draft-owner') owners.add(value);
        if (key === 'orca.project-draft-lineage') {
          let lineage: unknown;
          try { lineage = JSON.parse(value); } catch { throw new Error('Historical recovery lineage cannot be decoded. Original namespaces are retained.'); }
          if (!Array.isArray(lineage) || lineage.some((owner) => typeof owner !== 'string')) throw new Error('Historical recovery lineage is incomplete. Original namespaces are retained.');
          lineage.forEach((owner: string) => owners.add(owner));
        }
      }
    }
    if (recoveries.size > 1) throw Object.assign(new Error('Historical recovery namespaces differ. Choose an explicit recovery before adoption; no namespace was replaced.'), { name: 'ProjectConflictError' });
    return { databases: [...databases, desktop.database], desktop, renderer, preferences,
      recovery: recoveries.values().next().value ?? null, lineage: owners.size ? JSON.stringify([...owners]) : null, owner: null };
  }
  const databases = await Promise.all([
    captureDatabase('orca-web'),
    captureDatabase('orca-web-drafts'),
  ]);
  const read = (key: string) => typeof sessionStorage === 'undefined' ? null : sessionStorage.getItem(key);
  return { databases: databases.filter((db): db is LegacyDatabase => db !== null),
    recovery: read('orca.fsm.recovery.v1'), lineage: read('orca.project-draft-lineage'), owner: read('orca.project-draft-owner'), preferences: captureLegacyPreferences() };
}
export async function verifyLegacyProjectSource(inventory: LegacyProjectInventory): Promise<void> {
  if (!inventory.desktop && !inventory.renderer) return;
  const source = await import('./desktop-legacy'), host = typeof window === 'undefined' ? undefined : window.orcaDesktop;
  if (!inventory.desktop || !inventory.renderer || !host?.legacyProjects || !host.legacyRendererStorage) throw source.desktopLegacyIncomplete();
  await source.verifyDesktopRendererCapture(host.legacyRendererStorage, inventory.renderer);
  const current = await host.legacyProjects.inventory();
  if (current.version !== 1 || current.complete !== true || current.source !== 'electron-user-data'
    || current.fingerprint !== inventory.desktop.inventory.fingerprint) throw source.desktopLegacyIncomplete();
}
export const hasLegacyProjects = (inventory: LegacyProjectInventory) => !!inventory.desktop && (inventory.desktop.files.length > 0 || inventory.desktop.inventory.directories.length > 0)
  || inventory.databases.some((db) => db.stores.some((store) => store.records.length))
  || !!inventory.renderer && inventory.renderer.origins.some((origin) => origin.localStorage.length || origin.databases.length || origin.sessionStorageNamespaces.some((ns) => ns.values.length) || origin.binaryParts.length)
  || inventory.recovery !== null || inventory.lineage !== null || inventory.owner !== null || Object.keys(inventory.preferences ?? {}).length > 0;

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
      const result: Record<string, unknown> = Object.create(null);
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
