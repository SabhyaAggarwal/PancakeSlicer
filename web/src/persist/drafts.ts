import type { StoredDocBase } from './backend';
import { projectId } from './identity';
import { requireWorkspaceService, routeKey, serviceRequest } from './service';
import { createMachine, type Machine } from '../flow/fsm';
import { captureProjectWire, prepareOnce, projectJson as serviceJson, projectWrite } from './project-wire';
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
  asset(key: string): Promise<Blob | null>;
  put(draft: ProjectDraft): Promise<void>;
  remove(id: string, throughSequence: number): Promise<void>;
}
export interface ProjectDraftMutationSnapshot {
  readonly pending: number; readonly revision: number; readonly error: string | null;
}
type MutationMessage = { writer: string; token: string; id: string; phase: 'begin' | 'end'; error?: string } | { writer: string; phase: 'bye' };
const writer = projectId(), mutations = new Map<string, { writer: string; id: string }>(), failedMutations = new Map<string, string>();
const publishMutations = () => {
  mutationSnapshot = { pending: mutations.size, revision: mutationSnapshot.revision + 1, error: failedMutations.values().next().value ?? null };
  mutationListeners.forEach((listener) => listener());
};
const lockName = (id: string) => `drafts-writer:${id}`;
type WriterEvent = { type: 'BEGIN' } | { type: 'BYE' } | { type: 'LOCK_GONE' };
const writers = new Map<string, Machine<'live' | 'gone', WriterEvent, Record<string, never>>>();
function remoteWriter(id: string) {
  let m = writers.get(id);
  if (!m) writers.set(id, m = createMachine<'live' | 'gone', WriterEvent, Record<string, never>>({ id: 'draft-writer', initial: 'live', strict: true, context: {}, states: {
    live: { on: { BEGIN: {}, BYE: 'gone', LOCK_GONE: 'gone' } },
    gone: { entry: () => {
      for (const [token, entry] of mutations) if (entry.writer === id) mutations.delete(token);
      for (const key of failedMutations.keys()) if (key.startsWith(`${id}/`)) failedMutations.delete(key);
      writers.delete(id); publishMutations();
    }, on: { BEGIN: 'live', BYE: {}, LOCK_GONE: {} } },
  } }));
  return m;
}
export async function settleDraftWriters(): Promise<void> {
  const remote = new Set([...mutations.values()].map((entry) => entry.writer).filter((id) => id !== writer));
  for (const key of failedMutations.keys()) { const id = key.slice(0, key.indexOf('/')); if (id !== writer) remote.add(id); }
  if (!remote.size || typeof navigator === 'undefined' || !navigator.locks) return;
  const state = await navigator.locks.query(), held = new Set([...(state.held ?? []), ...(state.pending ?? [])].map((lock) => lock.name));
  for (const id of remote) if (!held.has(lockName(id))) remoteWriter(id).send({ type: 'LOCK_GONE' });
}
const mutationListeners = new Set<() => void>();
let mutationSerial = 0, mutationSnapshot: ProjectDraftMutationSnapshot = { pending: 0, revision: 0, error: null };
let mutationChannel: BroadcastChannel | null = null;
const receiveMutation = (message: MutationMessage) => {
  if (!message || typeof message.writer !== 'string') return;
  if (message.phase === 'bye') { if (message.writer !== writer) remoteWriter(message.writer).send({ type: 'BYE' }); return; }
  if (typeof message.token !== 'string' || typeof message.id !== 'string') return;
  const failureKey = `${message.writer}/${message.id}`;
  if (message.phase === 'begin') {
    if (message.writer !== writer) remoteWriter(message.writer).send({ type: 'BEGIN' });
    mutations.set(message.token, { writer: message.writer, id: message.id });
  } else if (message.phase === 'end') {
    mutations.delete(message.token);
    if (typeof message.error === 'string') failedMutations.set(failureKey, message.error); else failedMutations.delete(failureKey);
  } else return;
  publishMutations();
};
if (typeof window !== 'undefined') {
  if (typeof BroadcastChannel !== 'undefined') {
    mutationChannel = new BroadcastChannel('orca.project-drafts');
    mutationChannel.onmessage = (event) => { if (event.data?.writer !== writer) receiveMutation(event.data); };
    window.addEventListener('pagehide', () => {
      if (![...mutations.values()].some((entry) => entry.writer === writer)) mutationChannel?.postMessage({ writer, phase: 'bye' } satisfies MutationMessage);
    });
  }
  void navigator.locks?.request(lockName(writer), () => new Promise<never>(() => {})).catch(() => {});
}
export const projectDraftMutationSnapshot = (): ProjectDraftMutationSnapshot => mutationSnapshot;
export const subscribeProjectDraftMutations = (listener: () => void) => { mutationListeners.add(listener); return () => { mutationListeners.delete(listener); }; };
export function beginProjectDraftMutation(id: string): (error?: unknown) => void {
  const token = `${writer}.${++mutationSerial}`;
  const announce = (message: MutationMessage) => {
    receiveMutation(message);
    if (mutationChannel) mutationChannel.postMessage(message);
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
  try { await requireWorkspaceService(); const result = await operation(); finish(); return result; }
  catch (error) { finish(error); throw error; }
}
interface ServiceDraft { id: string; sequence: number; document: Omit<ProjectDraft, 'blobs'> | null; metadata?: ProjectDraftMeta; updatedAt: number }
function validMetadata(meta: ProjectDraftMeta | undefined): meta is ProjectDraftMeta {
  return !!meta && typeof meta.id === 'string' && typeof meta.projectId === 'string' && typeof meta.owner === 'string' &&
    typeof meta.name === 'string' && Number.isSafeInteger(meta.sequence) && meta.sequence >= 0 &&
    Number.isSafeInteger(meta.baseRevision) && meta.baseRevision >= 0 && Number.isFinite(meta.updatedAt) &&
    Number.isSafeInteger(meta.objects) && meta.objects >= 0 && (meta.kind === 'dirty' || meta.kind === 'snapshot') &&
    (meta.failure === null || !!meta.failure && ['conflict', 'deleted', 'quota', 'unavailable', 'interrupted', 'decode'].includes(meta.failure.kind) &&
      typeof meta.failure.name === 'string' && typeof meta.failure.message === 'string' && meta.failure.projectId === meta.projectId && Number.isSafeInteger(meta.failure.expectedRevision));
}
const assetPath = (key: string) => `/api/assets/${routeKey(key)}`;
const draftPath = (key: string) => `/api/drafts/${routeKey(key)}`;
const uploaded = new WeakMap<Blob, Set<string>>();
const pendingDrafts = new Map<string, Promise<RequestInit>>();
const serviceDraftStore: ProjectDraftStore = {
  async list(projectId) {
    const metadata = await this.listMetadata(projectId);
    const drafts: ProjectDraft[] = [];
    for (const meta of metadata) {
      const draft = await this.load(meta.id);
      if (!draft) throw new Error('A recovery draft changed while reading inventory. Refresh before mutation.');
      drafts.push(draft);
    }
    return drafts;
  },
  async listMetadata(projectId) {
    const records = await serviceJson<ServiceDraft[]>('/api/drafts');
    if (!Array.isArray(records)) throw new Error('Invalid service draft inventory');
    return records.map((record) => {
      if (!validMetadata(record.metadata) || record.metadata.id !== record.id || record.metadata.sequence !== record.sequence) throw new Error('Incomplete service draft metadata');
      return record.metadata;
    }).filter((meta) => projectId === undefined || meta.projectId === projectId).sort((a,b) => b.updatedAt - a.updatedAt);
  },
  async load(id) {
    const record = await serviceJson<ServiceDraft>(draftPath(id), undefined, true);
    if (!record) return null;
    const document = record.document;
    if (!document || !validMetadata(document) || !document.doc || document.id !== id || document.sequence !== record.sequence) throw new Error('Invalid service recovery document');
    return { ...document, blobs: {} };
  },
  async asset(key) {
    const response = await serviceRequest(assetPath(key), undefined, true);
    if (!response) throw new Error(`Recovery model asset is missing: ${key}`);
    return response.blob();
  },
  put: (draft) => mutate(draft.id, async () => {
    const { blobs: _blobs, ...document } = draft;
    const payload = { sequence: draft.sequence, document, metadata: draftMetadata(draft) };
    const capture = await captureProjectWire(payload);
    const key = `${draft.id}/${draft.sequence}/${capture.digest}`;
    const preparation = prepareOnce(pendingDrafts, key, async () => projectWrite(await capture.draft(projectId())));
    let request: RequestInit;
    try { request = await preparation; } finally { capture.release(); }
    for (const key of Object.keys(draft.doc.files ?? {})) {
      const blob = draft.blobs[key];
      if (!(blob instanceof Blob)) throw new Error(`Draft model file is missing: ${key}`);
      if (!uploaded.get(blob)?.has(key)) {
        if (!await serviceRequest(assetPath(key), { method: 'HEAD' }, true)) await serviceRequest(assetPath(key), { method: 'PUT', body: blob });
        const keys = uploaded.get(blob) ?? new Set<string>(); keys.add(key); uploaded.set(blob, keys);
      }
    }
    const ack = await serviceJson<{sequence:number}>(draftPath(draft.id), request);
    if (!ack || ack.sequence !== draft.sequence) throw new Error('Invalid draft acknowledgement');
    if (pendingDrafts.get(key) === preparation) pendingDrafts.delete(key);
  }),
  remove: (id, throughSequence) => mutate(id, async () => {
    try { await serviceRequest(`${draftPath(id)}?throughSequence=${throughSequence}`, { method: 'DELETE' }, true); }
    catch (error) {
      if ((error as {status?:number}).status === 409) return;
      throw error;
    }
  }),
};
export async function projectDraftStore(): Promise<ProjectDraftStore> {
  await requireWorkspaceService();
  return serviceDraftStore;
}
export const draftMetadata = ({ doc: _doc, blobs: _blobs, ...meta }: ProjectDraft): ProjectDraftMeta => meta;
