import type { StoredDocBase } from '../persist/backend';
import type { ProjectDraft, ProjectFailure } from '../persist/drafts';
export type { ProjectFailure } from '../persist/drafts';
export interface DraftCheckpoint { id: string; sequence: number }
export type CheckpointDocument = StoredDocBase & { recoveryCheckpoint?: DraftCheckpoint };
export type DraftDurability = { state: 'none' } | { state: 'pending'; checkpoint: DraftCheckpoint }
  | { state: 'durable'; checkpoint: DraftCheckpoint; updatedAt: number }
  | { state: 'failed'; checkpoint: DraftCheckpoint; message: string };
export function projectFailure(error: unknown, projectId: string, expectedRevision: number, loading = false): ProjectFailure {
  const source = error && typeof error === 'object' ? error as { name?: unknown; message?: unknown; failure?: ProjectFailure } : {};
  if (source.failure?.projectId === projectId) return { ...source.failure };
  const name = typeof source.name === 'string' ? source.name : 'Error';
  const kind = name === 'ProjectConflictError' ? 'conflict' : name === 'ProjectDeletedError' ? 'deleted'
    : name === 'QuotaExceededError' ? 'quota' : name === 'ProjectServiceError' ? 'unavailable' : loading ? 'decode' : 'unavailable';
  const message = kind === 'quota' ? 'Storage is full. Keep this window open, export the project, or free space and retry.'
    : typeof source.message === 'string' ? source.message : String(error);
  return { kind, name, message, projectId, expectedRevision };
}
export const conflictFailure = (projectId: string, expectedRevision: number): ProjectFailure => ({
  kind: 'conflict', name: 'ProjectConflictError', projectId, expectedRevision,
  message: 'This project changed in another window. Reload latest or reapply this draft explicitly.',
});
export function failureError(failure: ProjectFailure): Error & { failure: ProjectFailure } {
  return Object.assign(new Error(failure.message), { name: failure.name, failure });
}
export const blocksAutosave = (failure: ProjectFailure | null) => failure?.kind === 'conflict' || failure?.kind === 'deleted' || failure?.kind === 'interrupted';
export function checkpointCommitted(draft: ProjectDraft, doc: CheckpointDocument): boolean {
  return doc.recoveryCheckpoint?.id === draft.id && doc.recoveryCheckpoint.sequence >= draft.sequence;
}
export function recoveryDocument(doc: CheckpointDocument): StoredDocBase {
  const { recoveryCheckpoint: _checkpoint, ...document } = doc;
  return document;
}
export interface DraftCodec<D> {
  encode(document: D): { stored: StoredDocBase; blobs: Map<string, Blob> };
  nameOf(document: D): string; plateCount(document: D): number;
}
