import { isProjectId, projectId } from './identity';

export interface ProjectMeta {
  id: string;
  name: string;
  savedAt: number;
  revision?: number;
  objects: number;
  thumbnail: string | null;
}

export interface StoredDocBase {
  files?: Record<string, string>;
  schemaVersion?: 2;
}

export interface ProfileReferenceDocuments {
  complete: boolean;
  documents: { id: string; name: string; doc: StoredDocBase; trashed?: boolean }[];
}

export interface ProjectStore {
  referenceDocuments?(): Promise<ProfileReferenceDocuments>;
  readonly kind: 'browser' | 'desktop';
  list(): Promise<ProjectMeta[]>;
  load(id: string): Promise<{ meta: ProjectMeta; doc: StoredDocBase } | null>;
  save(id: string, meta: Omit<ProjectMeta, 'id' | 'thumbnail'>, doc: StoredDocBase, thumbnail?: string | null): Promise<void>;
  rename(id: string, name: string): Promise<void>;
  duplicate(id: string, newId: string, name: string): Promise<ProjectMeta>;
  remove(id: string): Promise<void>;
  restore(id: string): Promise<void>;
  putBlob(id: string, key: string, data: Blob): Promise<void>;
  getBlob(id: string, key: string): Promise<Blob | null>;
  hasBlob(id: string, key: string): Promise<boolean>;
  emptyTrash?(): Promise<void>;
}

export const newKey = projectId;

interface DesktopProjects {
  referenceDocuments?(): Promise<ProfileReferenceDocuments>;
  list(): Promise<ProjectMeta[]>;
  load(id: string): Promise<{ meta: ProjectMeta; doc: StoredDocBase } | null>;
  save(id: string, meta: Omit<ProjectMeta, 'id' | 'thumbnail'>, doc: StoredDocBase, thumbnail?: string | null): Promise<void>;
  rename(id: string, name: string): Promise<void>;
  duplicate(id: string, newId: string, name: string): Promise<ProjectMeta>;
  remove(id: string): Promise<void>;
  restore(id: string): Promise<void>;
  putBlob(id: string, key: string, data: ArrayBuffer): Promise<void>;
  getBlob(id: string, key: string): Promise<ArrayBuffer | Uint8Array | null>;
  hasBlob(id: string, key: string): Promise<boolean>;
}
declare global {
  interface Window { orcaDesktop?: { version: number; projects?: DesktopProjects;
    onBeforeClose?(handler: () => Promise<void>): () => void; flushBeforeClose?(): Promise<void> } }
}

function desktopStore(p: DesktopProjects): ProjectStore {
  return {
    ...p,
    list: async () => (await p.list()).filter(validMeta),
    kind: 'desktop',
    putBlob: async (id, key, data) => p.putBlob(id, key, await data.arrayBuffer()),
    getBlob: async (id, key) => { const b = await p.getBlob(id, key); return b && new Blob([b as BlobPart]); },
  };
}

interface Rec extends ProjectMeta { schemaVersion?: 2; blobs: string[]; doc: StoredDocBase }
function validMeta(v: unknown): v is ProjectMeta {
  if (!v || typeof v !== 'object') return false;
  const m = v as ProjectMeta;
  return typeof m.id === 'string' && /^[A-Za-z0-9._-]+$/.test(m.id) && typeof m.name === 'string' &&
    Number.isFinite(m.savedAt) && Number.isFinite(m.objects) && (m.thumbnail == null || typeof m.thumbnail === 'string');
}
function validRecord(v: unknown): v is Rec {
  if (!validMeta(v)) return false;
  const r = v as Rec;
  return !!r.doc && typeof r.doc === 'object' && !Array.isArray(r.doc) &&
    (r.blobs === undefined || Array.isArray(r.blobs) && r.blobs.every((k) => typeof k === 'string'));
}

const request = <T>(r: IDBRequest<T>) => new Promise<T>((resolve, reject) => {
  r.onsuccess = () => resolve(r.result);
  r.onerror = () => reject(r.error);
});
const complete = (tx: IDBTransaction) => new Promise<void>((resolve, reject) => {
  tx.oncomplete = () => resolve();
  tx.onerror = tx.onabort = () => reject(tx.error);
});

async function browserStore(): Promise<ProjectStore> {
  const open = indexedDB.open('orca-web', 3);
  open.onupgradeneeded = (e) => {
    const db = open.result;
    if (e.oldVersion < 1) {
      db.createObjectStore('sessions', { keyPath: 'id' });
      db.createObjectStore('blobs');
    }
    if (e.oldVersion < 2) db.createObjectStore('trash', { keyPath: 'id' });
    if (e.oldVersion < 3) { db.createObjectStore('aliases'); db.createObjectStore('quarantine'); db.createObjectStore('tombstones'); }
  };
  let abandoned = false;
  const db = await new Promise<IDBDatabase>((resolve, reject) => {
    open.onblocked = () => {
      abandoned = true;
      reject(new Error('Project storage upgrade is blocked. Close other Orca tabs, then retry.'));
    };
    open.onerror = () => reject(open.error);
    open.onsuccess = () => { if (abandoned) open.result.close(); else resolve(open.result); };
  });
  db.onversionchange = () => db.close();
  const tx = (stores: string[], mode: IDBTransactionMode, fill: (t: IDBTransaction) => void) => {
    const t = db.transaction(stores, mode);
    fill(t);
    return complete(t);
  };
  const atomic = <T>(stores: string[], run: (t: IDBTransaction, result: (v: T) => void, fail: (e: Error) => void) => void) =>
    new Promise<T>((resolve, reject) => {
      const t = db.transaction(stores, 'readwrite');
      let value: T, failure: Error | null = null;
      const fail = (e: Error) => { failure = e; t.abort(); };
      t.oncomplete = () => resolve(value);
      t.onerror = t.onabort = () => reject(failure ?? t.error ?? new Error('Project storage failed'));
      try { run(t, (v) => { value = v; }, fail); } catch (e) { fail(e instanceof Error ? e : new Error(String(e))); }
    });
  const conflict = () => Object.assign(new Error('This project changed in another tab. Reload latest or keep this version.'), { name: 'ProjectConflictError' });
  const deleted = () => Object.assign(new Error('This project was deleted. Export a copy or return to Projects.'), { name: 'ProjectDeletedError' });
  const get = (store: string, id: string) => request(db.transaction(store).objectStore(store).get(id) as IDBRequest<Rec | undefined>);
  const meta = ({ id, name, savedAt, objects, thumbnail, revision }: Rec): ProjectMeta => ({ id, name, savedAt, objects, thumbnail: thumbnail ?? null, revision: revision ?? 0 });

  await new Promise<void>((resolve, reject) => {
    const t = db.transaction(['sessions', 'aliases', 'quarantine'], 'readwrite');
    const sessions = t.objectStore('sessions'), aliases = t.objectStore('aliases');
    const all = sessions.getAll();
    all.onsuccess = () => {
      for (const old of all.result as unknown[]) {
        if (!validRecord(old)) {
          const id = old && typeof old === 'object' ? (old as { id?: unknown }).id : null;
          if (typeof id === 'string') { t.objectStore('quarantine').put(old, id); sessions.delete(id); }
          continue;
        }
        if (old.schemaVersion === 2 && isProjectId(old.id)) continue;
        const id = isProjectId(old.id) ? old.id : projectId();
        const files = old.doc.files ?? Object.fromEntries((old.blobs ?? []).map((k) => [k, '']));
        sessions.put({ ...old, id, thumbnail: old.thumbnail ?? null, schemaVersion: 2,
          blobs: Object.keys(files), doc: { ...old.doc, files, schemaVersion: 2 } });
        if (id !== old.id) { aliases.put(id, old.id); sessions.delete(old.id); }
      }
    };
    t.oncomplete = () => resolve();
    t.onerror = t.onabort = () => reject(t.error);
  });
  const canonical = async (id: string): Promise<string> =>
    (await request(db.transaction('aliases').objectStore('aliases').get(id)) as string | undefined) ?? id;

  return {
    kind: 'browser',
    async referenceDocuments() {
      const transaction = db.transaction(['sessions', 'trash', 'quarantine']);
      const [live, trash, quarantined] = await Promise.all([
        request(transaction.objectStore('sessions').getAll()), request(transaction.objectStore('trash').getAll()),
        request(transaction.objectStore('quarantine').count()),
      ]);
      const records = [...live.map((record) => ({ record, trashed: false })), ...trash.map((record) => ({ record, trashed: true }))];
      return { complete: quarantined === 0 && records.every(({ record }) => validRecord(record)),
        documents: records.flatMap(({ record, trashed }) => validRecord(record) ? [{ id: record.id, name: record.name, doc: record.doc, trashed }] : []) };
    },
    async list() {
      const all = await request(db.transaction('sessions').objectStore('sessions').getAll() as IDBRequest<Rec[]>);
      return all.filter(validRecord).sort((a, b) => b.savedAt - a.savedAt).map(meta);
    },
    async load(id) {
      const r = await get('sessions', await canonical(id));
      return validRecord(r) ? { meta: meta(r), doc: r.doc.files ? r.doc : { ...r.doc, files: Object.fromEntries(r.blobs.map((k) => [k, ''])) } } : null;
    },
    async save(id, m, doc, thumbnail) {
      await atomic<void>(['sessions', 'tombstones'], (t, _result, fail) => {
        const sessions = t.objectStore('sessions'), read = sessions.get(id);
        read.onsuccess = () => {
          const old = read.result as Rec | undefined;
          const tombstone = t.objectStore('tombstones').get(id);
          tombstone.onsuccess = () => {
            if (tombstone.result !== undefined) return fail(deleted());
            const revision = old?.revision ?? 0;
            if ((m.revision ?? 0) !== revision) return fail(conflict());
            const rec: Rec = { ...m, id, revision: revision + 1, schemaVersion: 2,
              doc: { ...doc, schemaVersion: 2 }, blobs: Object.keys(doc.files ?? {}),
              thumbnail: thumbnail === undefined ? old?.thumbnail ?? null : thumbnail };
            sessions.put(rec);
          };
        };
      });
    },
    async rename(id, name) {
      id = await canonical(id);
      await atomic<void>(['sessions'], (t, _result, fail) => {
        const sessions = t.objectStore('sessions'), read = sessions.get(id);
        read.onsuccess = () => {
          const r = read.result;
          if (!validRecord(r)) return fail(new Error('Project not found'));
          sessions.put({ ...r, name, revision: (r.revision ?? 0) + 1, savedAt: Date.now() });
        };
      });
    },
    async duplicate(id, newId, name) {
      id = await canonical(id);
      return atomic<ProjectMeta>(['sessions', 'tombstones'], (t, result, fail) => {
        const sessions = t.objectStore('sessions'), read = sessions.get(id);
        read.onsuccess = () => {
          const r = read.result;
          if (!validRecord(r)) return fail(new Error('Project not found'));
          const target = sessions.get(newId);
          target.onsuccess = () => {
            if (target.result) return fail(conflict());
            const tombstone = t.objectStore('tombstones').get(newId);
            tombstone.onsuccess = () => {
              if (tombstone.result !== undefined) return fail(deleted());
              const copy: Rec = { ...r, id: newId, name, revision: 1, savedAt: Date.now() };
              sessions.put(copy); result(meta(copy));
            };
          };
        };
      });
    },
    async remove(id) {
      id = await canonical(id);
      await atomic<void>(['sessions', 'trash', 'tombstones'], (t) => {
        const read = t.objectStore('sessions').get(id);
        read.onsuccess = () => {
          const r = read.result;
          if (!validRecord(r)) return;
          t.objectStore('trash').put(r);
          t.objectStore('tombstones').put({ revision: (r.revision ?? 0) + 1 }, id);
          t.objectStore('sessions').delete(id);
        };
      });
    },
    async restore(id) {
      await atomic<void>(['sessions', 'trash', 'tombstones'], (t, _result, fail) => {
        const read = t.objectStore('trash').get(id);
        read.onsuccess = () => {
          const r = read.result;
          if (!validRecord(r)) return;
          const tombstone = t.objectStore('tombstones').get(id);
          tombstone.onsuccess = () => {
            const live = t.objectStore('sessions').get(id);
            live.onsuccess = () => {
              if (live.result) return fail(conflict());
              t.objectStore('sessions').put({ ...r, revision: Math.max(r.revision ?? 0, tombstone.result?.revision ?? 0) + 1 });
              t.objectStore('trash').delete(id); t.objectStore('tombstones').delete(id);
            };
          };
        };
      });
    },
    async emptyTrash() {
      const t = db.transaction(['sessions', 'trash', 'blobs'], 'readwrite');
      const live = t.objectStore('sessions').getAll(), trash = t.objectStore('trash').getAll();
      let remaining = 2;
      const clear = () => {
        if (--remaining) return;
        const kept = new Set((live.result as Rec[]).flatMap((r) => r.blobs ?? []));
        for (const r of trash.result as Rec[])
          for (const key of r.blobs ?? []) if (!kept.has(key)) t.objectStore('blobs').delete(key);
        t.objectStore('trash').clear();
      };
      live.onsuccess = trash.onsuccess = clear;
      await complete(t);
    },
    putBlob: (_id, key, data) => tx(['blobs'], 'readwrite', (t) => t.objectStore('blobs').put(data, key)),
    getBlob: async (_id, key) => (await request(db.transaction('blobs').objectStore('blobs').get(key) as IDBRequest<Blob | undefined>)) ?? null,
    hasBlob: async (_id, key) => (await request(db.transaction('blobs').objectStore('blobs').count(key))) > 0,
  };
}

const storageListeners = new Set<() => void>();
let pendingReferenceWrites = 0;
export const projectReferenceWritePending = () => pendingReferenceWrites > 0;
export const subscribeProjectStorageChanges = (listener: () => void) => { storageListeners.add(listener); return () => { storageListeners.delete(listener); }; };
function observableStore(backend: ProjectStore): ProjectStore {
  const result = { ...backend };
  for (const key of ['save', 'rename', 'duplicate', 'remove', 'restore', 'emptyTrash'] as const) {
    const operation = backend[key];
    if (!operation) continue;
    Object.assign(result, { [key]: async (...args: unknown[]) => {
      pendingReferenceWrites++;
      storageListeners.forEach((listener) => listener());
      try { return await Reflect.apply(operation, backend, args); }
      finally { pendingReferenceWrites--; storageListeners.forEach((listener) => listener()); }
    } });
  }
  return result;
}

let store: Promise<ProjectStore | null> | null = null;
let storageError: string | null = null;
export const projectStorageError = () => storageError ?? 'Project storage is unavailable. Export this project to keep a copy.';
export function projectStore(retry = false): Promise<ProjectStore | null> {
  if (retry) { store = null; storageError = null; }
  store ??= (async () => {
    const desktop = window.orcaDesktop;
    if (desktop?.version === 1 && desktop.projects) return observableStore(desktopStore(desktop.projects));
    if (typeof indexedDB === 'undefined') return null;
    return browserStore().then(observableStore).catch((e: unknown) => { storageError = e instanceof Error ? e.message : String(e); return null; });
  })();
  return store;
}
