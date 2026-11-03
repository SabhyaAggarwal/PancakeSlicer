import { newKey, type ProjectMeta, type ProjectStore } from '../persist/backend';
import type { ProjectDraft, ProjectDraftMeta, ProjectDraftStore } from '../persist/drafts';
import type { StoredDoc } from '../persist/session';
import { clearPlate } from '../actions';
import { documentGeneration, leaveDocument, newDocument, type Doc } from '../state/history';
import { clearPlateJobs } from '../plater/plates';
import { setState } from '../state/store';
import { project, projectList, flushProject, type ProjectDraftInventory } from './project';
import { writeRoute } from './routes';
import { checkpointCommitted, conflictFailure, failureError, projectFailure, recoveryDocument, type DraftCheckpoint, type ProjectFailure, type CheckpointDocument } from './project-recovery';
export interface RecoveryRuntime {
  storage(): ProjectStore | null; removed: Set<string>;
  checkpoint(): { checkpoint: DraftCheckpoint; durable: Promise<ProjectDraft> };
  preserveSnapshot(): Promise<void>; hasUnsaved(): boolean; waitForWrites(): Promise<void>;
  beginOpen(): number; invalidateWrites(): void; isCurrent(mine: number): boolean; adopt(action: () => void): void;
  previous(id: string): void; restored(): ProjectDraftMeta | null; setRestored(draft: ProjectDraftMeta | null): void; resetCaptured(): void;
  checkpointRecovered(id: string, revision: number, failure: ProjectFailure): unknown;
  activeWrite(id: string, gen: number, epoch: number): boolean;
  known: Set<string>; session: { owner: string; previousOwners: readonly string[] };
  publish(type: 'saved' | 'deleted', id: string): void; refreshList(): void;
  drafts(): Promise<ProjectDraftStore>; refreshDrafts(): Promise<ProjectDraftInventory>;
}
const codec = () => import('../persist/session');
export async function adoptProjectDraft(runtime: RecoveryRuntime, draft: ProjectDraft, canonical: ProjectMeta | null, mine: number) {
  const fields = await (await codec()).decode(recoveryDocument(draft.doc) as StoredDoc, async (key) => draft.blobs[key] ?? null);
  if (!runtime.isCurrent(mine)) return;
  const meta: ProjectMeta = canonical ?? { id: draft.projectId, name: draft.name, revision: draft.baseRevision,
    savedAt: 0, objects: draft.objects, thumbnail: null };
  const failure = runtime.removed.has(draft.projectId) ? projectFailure({ name: 'ProjectDeletedError', message: 'This project was deleted. Recover the draft as a copy.' }, draft.projectId, draft.baseRevision)
    : canonical && (canonical.revision ?? 0) !== draft.baseRevision ? conflictFailure(draft.projectId, draft.baseRevision)
    : draft.failure ?? { kind: 'interrupted', name: 'ProjectInterruptedError', projectId: draft.projectId, expectedRevision: draft.baseRevision,
      message: 'Unsaved project recovered. Retry to save it, or reload the saved version.' } satisfies ProjectFailure;
  runtime.previous(project.get().context.id);
  runtime.adopt(() => {
    leaveDocument(); clearPlateJobs();
    setState({ ...fields, projectName: draft.name, tab: 'prepare', gizmo: 'none', dialog: null, loading: null,
      job: { state: 'idle', pct: 0, stage: '' } });
    newDocument(false);
  });
  runtime.setRestored(draft); runtime.resetCaptured();
  project.send({ type: 'RECOVERED', meta, draft, failure });
  runtime.checkpointRecovered(draft.projectId, draft.baseRevision, failure);
  writeRoute(`/p/${draft.projectId}`, true);
}
export async function keepProjectVersion(runtime: RecoveryRuntime) {
  const id = project.get().context.id, gen = documentGeneration();
  try {
    await runtime.checkpoint().durable;
    await runtime.waitForWrites();
    const rec = await runtime.storage()?.load(id);
    if (id !== project.get().context.id || gen !== documentGeneration()) return;
    if (!rec || runtime.removed.has(id)) throw failureError(projectFailure({ name: 'ProjectDeletedError', message: 'The saved project is missing. Recover the draft as a copy.' }, id, project.get().context.revision));
    project.send({ type: 'REBASE', revision: rec.meta.revision ?? 0 });
    runtime.resetCaptured();
    await flushProject();
  } catch (error) {
    const failure = projectFailure(error, id, project.get().context.revision);
    if (id === project.get().context.id && gen === documentGeneration()) project.send({ type: 'ERROR', error: failure.message, failure });
  }
}
export async function reloadProject(runtime: RecoveryRuntime) {
  const id = project.get().context.id, gen = documentGeneration();
  const receipt = runtime.hasUnsaved() ? runtime.checkpoint() : null, source = runtime.restored();
  await runtime.preserveSnapshot();
  if (id !== project.get().context.id || gen !== documentGeneration()) return;
  await openProject(runtime, id, { reload: true });
  if (project.get().context.id === id && project.get().value === 'saved' && receipt) {
    await (await runtime.drafts()).remove(receipt.checkpoint.id, receipt.checkpoint.sequence);
    if (source?.kind === 'dirty') await (await runtime.drafts()).remove(source.id, source.sequence);
    void runtime.refreshDrafts();
  }
}
export async function recoverProjectDraft(runtime: RecoveryRuntime, draftId: string, options: { asCopy?: boolean } = {}) {
  await runtime.preserveSnapshot();
  const draft = await (await runtime.drafts()).load(draftId);
  if (!draft) throw new Error('Project draft not found');
  const mine = runtime.beginOpen(); runtime.invalidateWrites(); await runtime.waitForWrites();
  if (!runtime.isCurrent(mine)) return;
  const recovered = options.asCopy ? { ...draft, projectId: newKey(), baseRevision: 0, failure: null, kind: 'snapshot' as const } : draft;
  const canonical = options.asCopy ? null : await runtime.storage()?.load(draft.projectId);
  if (!runtime.isCurrent(mine)) return;
  project.send({ type: 'OPEN' });
  await adoptProjectDraft(runtime, recovered, canonical?.meta ?? null, mine);
}
export async function discardProjectDraft(runtime: RecoveryRuntime, draftId: string) {
  const c = project.get().context;
  if (runtime.hasUnsaved() && (c.draft.state !== 'none' && c.draft.checkpoint.id === draftId || runtime.restored()?.id === draftId))
    throw new Error('Save or reload the current project before discarding its active recovery draft.');
  const storage = await runtime.drafts(), draft = await storage.load(draftId);
  if (draft) await storage.remove(draft.id, draft.sequence);
  await runtime.refreshDrafts();
}

export async function openProject(runtime: RecoveryRuntime, id: string, options: { history?: boolean; reload?: boolean } = {}) {
  if (!options.reload && project.get().context.id === id && project.get().value !== 'missing' && project.get().value !== 'opening' && !project.get().context.loadError) {
    setState({ tab: 'prepare' }); if (options.history !== false) writeRoute(`/p/${id}`); return;
  }
  const mine = runtime.beginOpen();
  if (!options.reload && !project.get().context.loadError) await flushProject();
  else { runtime.invalidateWrites(); await runtime.waitForWrites(); }
  if (!runtime.isCurrent(mine)) return;
  runtime.invalidateWrites();
  project.send({ type: 'OPEN' }); setState({ loading: 'Opening', tab: 'prepare' });
  if (options.history !== false) writeRoute(`/p/${encodeURIComponent(id)}`);
  try {
    let loadError: unknown;
    let rec: Awaited<ReturnType<ProjectStore['load']>> = null;
    try { const store = runtime.storage(); if (!store) throw new Error('Project storage is unavailable'); rec = await store.load(id); } catch (error) { loadError = error; }
    const drafts = await runtime.drafts().then((storage) => storage.list(rec?.meta.id ?? id)).catch(() => [] as ProjectDraft[]);
    if (!runtime.isCurrent(mine)) return;
    const draft = !options.reload ? drafts.find((d) => d.kind === 'dirty' && (runtime.session.previousOwners.includes(d.owner) || d.owner === runtime.session.owner)) : undefined;
    if (draft && (!rec || !checkpointCommitted(draft, rec.doc))) {
      await adoptProjectDraft(runtime, draft, rec?.meta ?? null, mine); return;
    }
    if (draft && rec && checkpointCommitted(draft, rec.doc)) {
      await (await runtime.drafts()).remove(draft.id, draft.sequence); void runtime.refreshDrafts();
    }
    if (loadError) throw loadError;
    if (!rec || runtime.removed.has(rec.meta.id)) { project.send({ type: 'MISSING', id }); setState({ loading: null }); return; }
    const fields = await (await codec()).decode(recoveryDocument(rec.doc) as StoredDoc, (k) => runtime.storage()!.getBlob(rec.meta.id, k));
    if (!runtime.isCurrent(mine)) return;
    if (runtime.removed.has(rec.meta.id)) { project.send({ type: 'MISSING', id }); setState({ loading: null }); return; }
    for (const k of Object.keys(rec.doc.files ?? {})) runtime.known.add(`${rec.meta.id}/${k}`);
    runtime.previous(project.get().context.id);
    runtime.adopt(() => {
      leaveDocument(); clearPlateJobs();
      setState({ ...fields, projectName: rec.meta.name, tab: 'prepare', gizmo: 'none', dialog: null, loading: null,
        job: { state: 'idle', pct: 0, stage: '' } });
      newDocument(true);
    });
    runtime.setRestored(null); runtime.resetCaptured();
    project.send({ type: 'OPENED', meta: rec.meta });
    writeRoute(`/p/${rec.meta.id}`, true);
  } catch (error) {
    if (!runtime.isCurrent(mine)) return;
    const failure = projectFailure(error, id, project.get().context.revision, true);
    setState({ loading: null }); project.send({ type: 'ERROR', error: failure.message, failure });
  }
}
export async function renameProject(runtime: RecoveryRuntime, id: string, name: string) {
  if (!name.trim()) return;
  if (id === project.get().context.id) { setState({ projectName: name.trim() }); return; }
  await runtime.waitForWrites();
  await runtime.storage()?.rename(id, name.trim()); runtime.publish('saved', id); runtime.refreshList();
}
export async function duplicateProject(runtime: RecoveryRuntime, id: string): Promise<ProjectMeta | null> {
  const store = runtime.storage();
  if (!store) return null;
  if (id === project.get().context.id) await flushProject();
  await runtime.waitForWrites();
  const name = projectList()?.find((p) => p.id === id)?.name ?? project.get().context.name;
  const meta = await store.duplicate(id, newKey(), `${name} copy`);
  runtime.publish('saved', meta.id); runtime.refreshList(); return meta;
}
export async function deleteProject(runtime: RecoveryRuntime, id: string): Promise<() => Promise<void>> {
  const storage = runtime.storage();
  if (!storage) return async () => {};
  await runtime.waitForWrites();
  await storage.remove(id);
  runtime.removed.add(id);
  if (id === project.get().context.id) {
    runtime.invalidateWrites(); runtime.beginOpen(); project.send({ type: 'DELETED' });
    runtime.adopt(() => { clearPlateJobs(); clearPlate(); newDocument(true); });
    setState({ tab: 'projects' }); writeRoute('/projects');
  }
  runtime.publish('deleted', id); runtime.refreshList();
  return async () => { await storage.restore(id); runtime.removed.delete(id); runtime.publish('saved', id); runtime.refreshList(); };
}

export async function saveProjectSnapshot(runtime: RecoveryRuntime, capture: {
  id: string; document: Doc; gen: number; version: number; revision: number; storage: ProjectStore | null;
  receipt: { checkpoint: DraftCheckpoint; durable: Promise<ProjectDraft> };
}) {
  const { id, document, gen, version, revision, storage, receipt } = capture;
    await receipt.durable.catch(() => null);
    if (!storage) throw new Error('Project storage is unavailable');
    if (runtime.removed.has(id)) return;
    if (runtime.activeWrite(id, gen, version) && project.get().context.conflict) throw failureError(project.get().context.failure ?? conflictFailure(id, revision));
    const { encode, nameOf, plateCount, thumbnail } = await codec();
    const { stored, blobs } = encode(document);
    for (const [k, b] of blobs) {
      if (runtime.known.has(`${id}/${k}`)) continue;
      if (!(await storage.hasBlob(id, k))) await storage.putBlob(id, k, b);
      runtime.known.add(`${id}/${k}`);
    }
    if (runtime.removed.has(id)) return;
    const meta = { name: nameOf(document), savedAt: Date.now(), objects: plateCount(document), revision };
    const committed: CheckpointDocument = { ...stored, recoveryCheckpoint: receipt.checkpoint };
    await storage.save(id, meta, committed, runtime.activeWrite(id, gen, version) ? thumbnail() ?? undefined : undefined);
    if (runtime.activeWrite(id, gen, version)) project.send({ type: 'SAVED', id, ...meta, revision: revision + 1, checkpoint: receipt.checkpoint });
    if (!runtime.activeWrite(id, gen, version) || !project.get().context.conflict) {
      const drafts = await runtime.drafts().catch(() => null);
      await drafts?.remove(receipt.checkpoint.id, receipt.checkpoint.sequence).catch(() => {});
      if (runtime.activeWrite(id, gen, version) && runtime.restored()?.projectId === id && runtime.restored()?.kind === 'dirty') {
        await drafts?.remove(runtime.restored()!.id, runtime.restored()!.sequence).catch(() => {}); runtime.setRestored(null);
      }
      void runtime.refreshDrafts();
    }
    runtime.publish('saved', id); runtime.refreshList();
}
