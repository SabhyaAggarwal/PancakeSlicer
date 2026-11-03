import type { ProjectDraft, ProjectDraftStore, ProjectFailure } from '../persist/drafts';
import type { DraftCheckpoint, DraftCodec } from './project-recovery';
import type { ProjectDraftInventory } from './project';
import { flushServiceSession, loadServiceSession, patchServiceSession } from '../persist/service-session';
import { requireWorkspaceService } from '../persist/service';
export async function draftSession(newOwner: string): Promise<{ owner: string; previousOwner: string | null; previousOwners: readonly string[] }> {
  const remote = await loadServiceSession(), lineage = remote.document.projectDraftOwners ?? [];
  if (!Array.isArray(lineage) || lineage.some((owner) => typeof owner !== 'string')) throw new Error('Invalid service draft lineage');
  const previousOwners = [...new Set(lineage as string[])].slice(0, 32), previousOwner = previousOwners[0] ?? null;
  return { owner: newOwner, previousOwner, previousOwners };
}
async function registerDraftOwner(owner: string): Promise<void> {
  await requireWorkspaceService();
  await flushServiceSession(true);
  const remote = await loadServiceSession(), lineage = remote.document.projectDraftOwners ?? [];
  if (!Array.isArray(lineage) || lineage.some(value => typeof value !== 'string')) throw new Error('Invalid service draft lineage');
  if (lineage.includes(owner)) return;
  await patchServiceSession({ projectDraftOwners: [owner, ...new Set(lineage as string[])].slice(0, 32) });
  await flushServiceSession();
}
export function createDraftJournal<D>(owner: string, storage: () => Promise<ProjectDraftStore>, codec: () => Promise<DraftCodec<D>>,
  beginMutation: (id: string) => (error?: unknown) => void = () => () => {}) {
  let serial = 0, tail = Promise.resolve(), registered: Promise<void> | null = null;
  const register = () => registered ??= registerDraftOwner(owner).catch(error => { registered = null; throw error; });
  const failures = new Map<string, unknown>();
  type Capture = { checkpoint: DraftCheckpoint; baseRevision: number; document: D; failure: ProjectFailure | null };
  const pending = new Map<string, { latest: Capture; durable: Promise<ProjectDraft>; release(): void }>();
  return {
    capture(projectId: string, baseRevision: number, document: D, failure: ProjectFailure | null = null) {
      const checkpoint = { id: `${owner}.${projectId}`, sequence: ++serial };
      const finish = beginMutation(checkpoint.id);
      const latest = { checkpoint, baseRevision, document, failure }, queued = pending.get(projectId);
      if (queued) {
        queued.latest = latest; void queued.durable.then(() => finish(), (error: unknown) => finish(error));
        return { checkpoint, durable: queued.durable };
      }
      let release!: () => void;
      const delay = new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, 250);
        release = () => { clearTimeout(timer); resolve(); };
      });
      const batch = { latest, durable: null! as Promise<ProjectDraft>, release };
      const durable = tail.then(async () => {
        await delay;
        pending.delete(projectId);
        const { checkpoint, baseRevision, document, failure } = batch.latest;
        const { encode, nameOf, plateCount } = await codec(), { stored, blobs } = encode(document);
        const draft: ProjectDraft = { ...checkpoint, owner, projectId, baseRevision, doc: stored,
          name: nameOf(document), objects: plateCount(document), updatedAt: Date.now(), kind: 'dirty', failure,
          blobs: Object.fromEntries(blobs) };
        await register();
        await (await storage()).put(draft); failures.delete(projectId); return draft;
      });
      batch.durable = durable; pending.set(projectId, batch);
      void durable.then(() => finish(), (error: unknown) => finish(error));
      tail = durable.then(() => {}, (error) => { failures.set(projectId, error); });
      return { checkpoint, durable };
    },
    async flush(projectId?: string) {
      for (const batch of pending.values()) batch.release();
      for (;;) { const waiting = tail; await waiting; if (waiting === tail) break; }
      const error = projectId === undefined ? failures.values().next().value : failures.get(projectId);
      if (error !== undefined) throw error;
    },
  };
}

export async function readDraftInventory(storage: () => Promise<ProjectDraftStore>, previous: ProjectDraftInventory): Promise<ProjectDraftInventory> {
  try { return { status: 'ready', drafts: await (await storage()).listMetadata(), error: null }; }
  catch (error) { return { ...previous, status: 'error', error: error instanceof Error ? error.message : String(error) }; }
}
