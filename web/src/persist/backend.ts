import { projectId } from './identity';
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
export interface ProjectCommandAck { revision: number; operationId: string }
export interface ProjectCommandStatus {
  operationId: string; id: string; kind: 'rename' | 'duplicate' | 'archive' | 'restore'; name?: string; newId?: string;
  expectedRevision: number | null; state: 'preparing' | 'sending' | 'failed' | 'conflict' | 'acknowledged'; error: string | null;
}

export interface ProjectStore {
  referenceDocuments(): Promise<ProfileReferenceDocuments>;
  readonly kind: 'service';
  list(): Promise<ProjectMeta[]>;
  load(id: string): Promise<{ meta: ProjectMeta; doc: StoredDocBase } | null>;
  save(id: string, meta: Omit<ProjectMeta, 'id' | 'thumbnail'>, doc: StoredDocBase, thumbnail?: string | null): Promise<{ revision: number; savedAt: number }>;
  rename(id: string, name: string): Promise<ProjectCommandAck>;
  duplicate(id: string, newId: string, name: string): Promise<ProjectMeta>;
  remove(id: string): Promise<ProjectCommandAck>;
  restore(id: string, expectedRevision?: number): Promise<ProjectCommandAck>;
  commands(): readonly Readonly<ProjectCommandStatus>[];
  subscribeCommands(listener: () => void): () => void;
  discardRejectedCommand(operationId: string): void;
  putBlob(id: string, key: string, data: Blob): Promise<void>;
  getBlob(id: string, key: string): Promise<Blob | null>;
  hasBlob(id: string, key: string): Promise<boolean>;
  emptyTrash?(): Promise<void>;
}

export const newKey = projectId;


declare global {
  interface Window { orcaDesktop?: { version: number;
    onBeforeClose?(handler: () => Promise<void>): () => void; flushBeforeClose?(): Promise<void> } }
}
const storageListeners = new Set<() => void>();
let pendingReferenceWrites = 0;
export const projectReferenceWritePending = () => pendingReferenceWrites > 0;
export const subscribeProjectStorageChanges = (listener: () => void) => { storageListeners.add(listener); return () => { storageListeners.delete(listener); }; };
function observableStore(backend: ProjectStore): ProjectStore {
  const result = { ...backend };
  for (const key of ['save', 'rename', 'duplicate', 'remove', 'restore', 'putBlob', 'emptyTrash'] as const) {
    const operation = backend[key];
    if (!operation) continue;
    Object.assign(result, { [key]: async (...args: unknown[]) => {
      pendingReferenceWrites++;
      storageListeners.forEach((listener) => listener());
      try { await (await import('./service')).requireWorkspaceService(); return await Reflect.apply(operation, backend, args); }
      finally { pendingReferenceWrites--; storageListeners.forEach((listener) => listener()); }
    } });
  }
  return result;
}


let store: Promise<ProjectStore | null> | null = null;
let storageError: string | null = null;
export const projectStorageError = () => storageError ?? 'The project service is unavailable. Keep this window open and retry.';
export function projectStore(retry = false): Promise<ProjectStore | null> {
  if (retry) { store = null; storageError = null; }
  store ??= (async () => {
    await (await import('./service')).requireWorkspaceService();
    const { serviceStore } = await import('./project-service');
    let primed: Awaited<ReturnType<ProjectStore['list']>> | null = await serviceStore.list();
    const unsubscribe = subscribeProjectStorageChanges(() => { primed = null; unsubscribe(); });
    return { ...observableStore(serviceStore), list: () => {
      const first = primed;
      primed = null;
      return first ? Promise.resolve(first) : serviceStore.list();
    } };
  })().catch((error: unknown) => { storageError = error instanceof Error ? error.message : String(error); return null; });
  return store;
}
