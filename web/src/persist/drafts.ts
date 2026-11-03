import type { StoredDocBase } from './backend';
import { projectId } from './identity';
export interface ProjectFailure {
  kind: 'conflict' | 'deleted' | 'quota' | 'unavailable' | 'interrupted' | 'decode';
  name: string; message: string; projectId: string; expectedRevision: number;
}
export interface ProjectDraftMeta {
  id: string; owner: string; projectId: string; sequence: number; baseRevision: number;
  name: string; objects: number; updatedAt: number; kind: 'dirty' | 'snapshot'; failure: ProjectFailure | null;
}
export interface ProjectDraft extends ProjectDraftMeta { doc: StoredDocBase; blobs: Record<string, Blob> }
export interface ProjectDraftStore {
  list(projectId?: string): Promise<ProjectDraft[]>;
  listMetadata(projectId?: string): Promise<ProjectDraftMeta[]>;
  load(id: string): Promise<ProjectDraft | null>;
  put(draft: ProjectDraft): Promise<void>;
  remove(id: string, throughSequence: number): Promise<void>;
}
export interface ProjectDraftMutationSnapshot {
  readonly pending: number; readonly revision: number; readonly error: string | null;
}
type MutationMessage = { writer: string; token: string; id: string; phase: 'begin' | 'end'; error?: string };
const writer = projectId(), mutations = new Map<string, string>(), failedMutations = new Map<string, string>();
const mutationListeners = new Set<() => void>();
let mutationSerial = 0, mutationSnapshot: ProjectDraftMutationSnapshot = { pending: 0, revision: 0, error: null };
let mutationChannel: BroadcastChannel | null = null;
const receiveMutation = (message: MutationMessage) => {
  if (!message || typeof message.token !== 'string' || typeof message.id !== 'string' || typeof message.writer !== 'string') return;
  const failureKey = `${message.writer}/${message.id}`;
  if (message.phase === 'begin') mutations.set(message.token, message.id);
  else if (message.phase === 'end') {
    mutations.delete(message.token);
    if (typeof message.error === 'string') failedMutations.set(failureKey, message.error); else failedMutations.delete(failureKey);
  } else return;
  mutationSnapshot = { pending: mutations.size, revision: mutationSnapshot.revision + 1, error: failedMutations.values().next().value ?? null };
  mutationListeners.forEach((listener) => listener());
};
if (typeof window !== 'undefined') {
  if (typeof BroadcastChannel !== 'undefined') {
    mutationChannel = new BroadcastChannel('orca.project-drafts');
    mutationChannel.onmessage = (event) => { if (event.data?.writer !== writer) receiveMutation(event.data); };
  } else window.addEventListener('storage', (event) => {
    if (event.key === 'orca.project-draft-mutation' && event.newValue) {
      try { const message = JSON.parse(event.newValue) as MutationMessage; if (message.writer !== writer) receiveMutation(message); } catch {  }
    }
  });
}
export const projectDraftMutationSnapshot = (): ProjectDraftMutationSnapshot => mutationSnapshot;
export const subscribeProjectDraftMutations = (listener: () => void) => { mutationListeners.add(listener); return () => { mutationListeners.delete(listener); }; };
export function beginProjectDraftMutation(id: string): (error?: unknown) => void {
  const token = `${writer}.${++mutationSerial}`;
  const announce = (message: MutationMessage) => {
    receiveMutation(message);
    if (mutationChannel) mutationChannel.postMessage(message);
    else if (typeof window !== 'undefined') {
      try { localStorage.setItem('orca.project-draft-mutation', JSON.stringify(message)); } catch {  }
    }
  };
  announce({ writer, token, id, phase: 'begin' });
  let ended = false;
  return (error) => {
    if (ended) return; ended = true;
    announce({ writer, token, id, phase: 'end', ...(error === undefined ? {} : { error: error instanceof Error ? error.message : String(error) }) });
  };
}
async function mutate<T>(id: string, operation: () => Promise<T>): Promise<T> {
  const finish = beginProjectDraftMutation(id);
  try { const result = await operation(); finish(); return result; }
  catch (error) { finish(error); throw error; }
}
let opening: Promise<ProjectDraftStore> | null = null;
export function projectDraftStore(): Promise<ProjectDraftStore> {
  if (opening) return opening;
  opening = openDraftStore().catch((error) => { opening = null; throw error; });
  return opening;
}
async function openDraftStore(): Promise<ProjectDraftStore> {
  if (typeof indexedDB === 'undefined') throw new Error('Draft storage is unavailable. Keep this window open and export the project.');
  const request = indexedDB.open('orca-web-drafts', 2);
  request.onupgradeneeded = (event) => {
    if (event.oldVersion < 1) request.result.createObjectStore('drafts', { keyPath: 'id' }).createIndex('projectId', 'projectId');
    if (event.oldVersion < 2) {
      const metadata = request.result.createObjectStore('metadata', { keyPath: 'id' });
      metadata.createIndex('projectId', 'projectId');
      const read = request.transaction!.objectStore('drafts').openCursor();
      read.onsuccess = () => { if (read.result) { metadata.put(draftMetadata(read.result.value as ProjectDraft)); read.result.continue(); } };
    }
  };
  const db = await new Promise<IDBDatabase>((resolve, reject) => {
    let abandoned = false;
    request.onsuccess = () => { if (abandoned) request.result.close(); else resolve(request.result); };
    request.onerror = () => reject(request.error);
    request.onblocked = () => { abandoned = true; reject(new Error('Draft storage upgrade is blocked by another Orca window.')); };
  });
  db.onversionchange = () => { db.close(); opening = null; };
  const transaction = <T>(mode: IDBTransactionMode, run: (records: IDBObjectStore, result: (value: T) => void, metadata: IDBObjectStore) => void) =>
    new Promise<T>((resolve, reject) => {
      const tx = db.transaction(['drafts', 'metadata'], mode);
      let value: T, failure: unknown;
      tx.oncomplete = () => resolve(value);
      tx.onerror = tx.onabort = () => reject(failure ?? tx.error ?? new Error('Draft transaction failed'));
      try { run(tx.objectStore('drafts'), (v) => { value = v; }, tx.objectStore('metadata')); }
      catch (error) { failure = error; tx.abort(); }
    });
  return {
    list: (projectId) => transaction('readonly', (records, result) => {
      const read = projectId === undefined ? records.getAll() : records.index('projectId').getAll(projectId);
      read.onsuccess = () => result((read.result as ProjectDraft[]).sort((a, b) => b.updatedAt - a.updatedAt));
    }),
    listMetadata: (projectId) => transaction('readonly', (_records, result, metadata) => {
      const read = projectId === undefined ? metadata.getAll() : metadata.index('projectId').getAll(projectId);
      read.onsuccess = () => result((read.result as ProjectDraftMeta[]).sort((a, b) => b.updatedAt - a.updatedAt));
    }),
    load: (id) => transaction('readonly', (records, result) => {
      const read = records.get(id); read.onsuccess = () => result((read.result as ProjectDraft | undefined) ?? null);
    }),
    put: (draft) => mutate(draft.id, () => {
      for (const key of Object.keys(draft.doc.files ?? {}))
        if (!(draft.blobs[key] instanceof Blob)) return Promise.reject(new Error(`Draft model file is missing: ${key}`));
      return transaction<void>('readwrite', (records, _result, metadata) => {
        const read = metadata.get(draft.id);
        read.onsuccess = () => {
          const previous = read.result as ProjectDraft | undefined;
          if (!previous || previous.sequence <= draft.sequence) { records.put(draft); metadata.put(draftMetadata(draft)); }
        };
      });
    }),
    remove: (id, throughSequence) => mutate(id, () => transaction<void>('readwrite', (records, _result, metadata) => {
      const read = metadata.get(id);
      read.onsuccess = () => { if (read.result && (read.result as ProjectDraftMeta).sequence <= throughSequence) { records.delete(id); metadata.delete(id); } };
    })),
  };
}
export const draftMetadata = ({ doc: _doc, blobs: _blobs, ...meta }: ProjectDraft): ProjectDraftMeta => meta;
