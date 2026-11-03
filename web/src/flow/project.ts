import { useSyncExternalStore } from 'react';
import { clearPlate } from '../actions';
import { clearPlateJobs } from '../plater/plates';
import { newKey, projectStore, projectStorageError, type ProjectMeta, type ProjectStore } from '../persist/backend';
import type { ProjectDraftMeta } from '../persist/drafts';
import type { DraftCheckpoint, DraftDurability, ProjectFailure } from './project-recovery';
import { documentGeneration, leaveDocument, newDocument, onLeaveDocument, snapshot, subscribeHistory, type Doc } from '../state/history';
import { getState, setState, subscribeStore, toast } from '../state/store';
import { assertNever, createMachine, useMachine, type Machine, type Snapshot } from './fsm';
import { currentRoute, writeRoute } from './routes';
import type { createDraftJournal } from './project-journal';

export type { ProjectMeta } from '../persist/backend';
export { newProject } from '../actions';
export type ProjectStateName = 'blank' | 'opening' | 'editing' | 'saving' | 'saved' | 'failed' | 'deleted' | 'missing';
export interface ProjectContext {
  id: string;
  name: string;
  savedAt: number;
  revision: number;
  error: string | null;
  dirty: boolean;
  conflict: boolean;
  loadError: boolean;
  origin: 'new' | 'loaded' | 'saved';
  failure: ProjectFailure | null;
  draft: DraftDurability;
}
export type ProjectEvent =
  | { type: 'EDIT' } | { type: 'FLUSH' } | { type: 'RETRY' } | { type: 'BLANK' } | { type: 'OPEN' }
  | { type: 'SAVED'; id: string; name: string; savedAt: number; revision: number; checkpoint?: DraftCheckpoint }
  | { type: 'ERROR'; error: string; failure?: ProjectFailure }
  | { type: 'DRAFT'; id: string; durability: Exclude<DraftDurability, { state: 'none' }> }
  | { type: 'RECOVERED'; meta: ProjectMeta; draft: ProjectDraftMeta; failure: ProjectFailure }
  | { type: 'OPENED'; meta: ProjectMeta } | { type: 'MISSING'; id: string } | { type: 'DELETED' }
  | { type: 'REBASE'; revision: number } | { type: 'REMOTE' } | { type: 'ACKNOWLEDGE' };
let recoveryCore: typeof import('./project-recovery');
const projectFailure = (...args: Parameters<typeof recoveryCore.projectFailure>) => recoveryCore.projectFailure(...args);
const conflictFailure = (...args: Parameters<typeof recoveryCore.conflictFailure>) => recoveryCore.conflictFailure(...args);
const failureError = (...args: Parameters<typeof recoveryCore.failureError>) => recoveryCore.failureError(...args);
const blocksAutosave = (failure: ProjectFailure | null) => failure ? recoveryCore?.blocksAutosave(failure) ?? true : false;
const AUTOSAVE_MS = 1000;
const codec = () => import('../persist/session');
let store: ProjectStore | null = null;
let observed: Doc | null = null;
let queue: Promise<void> = Promise.resolve();
const known = new Set<string>();
const removed = new Set<string>();
let generation = documentGeneration();
let epoch = 0;
let opening = 0;
let adopting = false;
let routing = false;
let previous: string | null = null;
const sameDoc = (a: Doc | null, b: Doc) => !!a && (Object.keys(b) as (keyof Doc)[]).every((k) => k === 'selected' || a[k] === b[k]);
const errorText = (e: unknown) => e instanceof Error ? e.message : String(e);
const blank = (): ProjectContext => ({ id: newKey(), name: 'Untitled', savedAt: 0, revision: 0, error: null, dirty: false, conflict: false, loadError: false, origin: 'new', failure: null, draft: { state: 'none' } });
const activeWrite = (id: string, gen: number, version: number) =>
  id === project.get().context.id && gen === documentGeneration() && version === epoch;

const draftStorage = async () => (await import('../persist/drafts')).projectDraftStore();
const session = { owner: newKey(), previousOwners: [] as readonly string[] };
const journalTools = () => import('./project-journal');
let draftActivity: typeof import('../persist/drafts') | null = null;
let journal: ReturnType<typeof createDraftJournal<Doc>>;
let captured: { id: string; document: Doc; revision: number; failure: ProjectFailure | null; receipt: ReturnType<typeof journal.capture> } | null = null;
let restoredDraft: ProjectDraftMeta | null = null;
export type ProjectDraftInventory = { status: 'loading' | 'ready' | 'error'; drafts: readonly ProjectDraftMeta[]; error: string | null };
let draftInventory: ProjectDraftInventory = { status: 'loading', drafts: [], error: null }, draftListToken = 0;
const draftListeners = new Set<() => void>();
export const projectDraftSnapshot = () => draftInventory;
export const subscribeProjectDrafts = (listener: () => void) => { draftListeners.add(listener); return () => { draftListeners.delete(listener); }; };
export async function refreshProjectDrafts(): Promise<ProjectDraftInventory> {
  const mine = ++draftListToken;
  const result = await (await journalTools()).readDraftInventory(draftStorage, draftInventory);
  if (mine === draftListToken) draftInventory = result;
  if (mine === draftListToken) draftListeners.forEach((listener) => listener());
  return draftInventory;
}
function checkpointProject(document = snapshot(), id = project.get().context.id, revision = project.get().context.revision,
  failure = project.get().context.failure) {
  if (project.get().context.draft.state !== 'failed' && captured?.id === id && captured.revision === revision && captured.failure === failure && sameDoc(captured.document, document)) return captured.receipt;
  const gen = documentGeneration(), version = epoch, receipt = journal.capture(id, revision, document, failure);
  captured = { id, document, revision, failure, receipt };
  project.send({ type: 'DRAFT', id, durability: { state: 'pending', checkpoint: receipt.checkpoint } });
  void receipt.durable.then((draft) => {
    if (activeWrite(id, gen, version)) project.send({ type: 'DRAFT', id, durability: { state: 'durable', checkpoint: receipt.checkpoint, updatedAt: draft.updatedAt } });
    void refreshProjectDrafts();
  }, (error: unknown) => {
    if (activeWrite(id, gen, version)) project.send({ type: 'DRAFT', id, durability: { state: 'failed', checkpoint: receipt.checkpoint, message: errorText(error) } });
  });
  return receipt;
}
function hasUnsavedProject() {
  const { value, context } = project.get();
  return value === 'editing' || value === 'saving' || value === 'failed' && !context.loadError || context.conflict;
}
async function preserveProjectSnapshot() {
  if (!hasUnsavedProject()) return;
  const record = await checkpointProject().durable;
  await (await draftStorage()).put({ ...record, id: newKey(), kind: 'snapshot' });
  await refreshProjectDrafts();
}

function save(id: string, m: Machine<ProjectStateName, ProjectEvent, ProjectContext>) {
  const doc = snapshot(), gen = documentGeneration(), version = epoch, revision = m.get().context.revision;
  const storage = store, receipt = checkpointProject(doc, id, revision);
  queue = queue.then(async () => {
    await (await recoveryActions()).saveProjectSnapshot(recoveryRuntime, { id, document: doc, gen, version, revision, storage, receipt });
  }).catch((error: unknown) => {
    const failure = projectFailure(error, id, revision);
    if (activeWrite(id, gen, version)) {
      m.send({ type: 'ERROR', error: failure.message, failure });
      checkpointProject(snapshot(), id, revision, failure);
    }
  });
}

const failed = { target: 'failed', action: (c: ProjectContext, e: { error: string; failure?: ProjectFailure }) => {
  const failure = c.conflict && c.failure ? c.failure : e.failure ?? projectFailure(e.error, c.id, c.revision);
  return { error: failure.message, failure, conflict: failure.kind === 'conflict', dirty: true };
} } as const;
const draftUpdate = { guard: (c: ProjectContext, e: Extract<ProjectEvent, { type: 'DRAFT' }>) => c.id === e.id &&
  (c.draft.state === 'none' || c.draft.checkpoint.sequence <= e.durability.checkpoint.sequence),
  action: (_: ProjectContext, e: Extract<ProjectEvent, { type: 'DRAFT' }>) => ({ draft: e.durability }) };
const recovered = { target: 'failed', action: (_: ProjectContext, e: Extract<ProjectEvent, { type: 'RECOVERED' }>): Partial<ProjectContext> => ({
  ...e.meta, revision: e.draft.baseRevision, origin: 'loaded', error: e.failure.message, failure: e.failure,
  conflict: e.failure.kind === 'conflict', dirty: true, loadError: false,
  draft: { state: 'durable', checkpoint: { id: e.draft.id, sequence: e.draft.sequence }, updatedAt: e.draft.updatedAt },
}) } as const;
const leave = { ERROR: failed, DRAFT: draftUpdate, RECOVERED: recovered, BLANK: 'blank', OPEN: 'opening', DELETED: 'deleted',
  MISSING: { target: 'missing', action: (_: ProjectContext, e: { id: string }) => ({ id: e.id, dirty: false }) } } as const;
const writable = (c: ProjectContext) => !c.conflict && !blocksAutosave(c.failure);
const notices = {
  REMOTE: { action: (c: ProjectContext) => ({ conflict: true, failure: conflictFailure(c.id, c.revision) }) },
  ACKNOWLEDGE: { guard: (c: ProjectContext) => !c.conflict, action: () => ({ conflict: false }) },
  REBASE: { target: 'editing', action: (_: ProjectContext, e: { revision: number }) => ({ revision: e.revision, conflict: false, error: null, failure: null }) },
} as const;
const savedPatch = (c: ProjectContext, e: Extract<ProjectEvent, { type: 'SAVED' }>): Partial<ProjectContext> => ({
  name: e.name, savedAt: e.savedAt, revision: e.revision, origin: 'saved', dirty: false, error: null, failure: null,
  ...(e.checkpoint && c.draft.state !== 'none' && c.draft.checkpoint.sequence <= e.checkpoint.sequence ? { draft: { state: 'none' } } : {}),
});

export const project = createMachine<ProjectStateName, ProjectEvent, ProjectContext>({
  id: 'project', initial: 'blank', context: blank(),
  states: {
    blank: { entry: blank,
      on: { ...leave, ...notices, EDIT: 'saving' } },
    opening: { on: { ...leave,
      OPENED: { target: 'saved', action: (_, e) => ({ ...e.meta, revision: e.meta.revision ?? 0, origin: 'loaded', error: null, failure: null, draft: { state: 'none' }, dirty: false, conflict: false, loadError: false }) },
      ERROR: { target: 'failed', action: (c, e) => ({ error: e.error, failure: e.failure ?? projectFailure(e.error, c.id, c.revision, true), loadError: true }) },
    } },
    editing: {
      after: { ms: AUTOSAVE_MS, target: 'saving', guard: writable },
      on: { ...leave, ...notices, EDIT: 'editing', FLUSH: { target: 'saving', guard: writable } } },
    saving: { entry: (ctx, _e, m) => { save(ctx.id, m); return { dirty: false }; },
      on: { ...leave, ...notices, EDIT: { action: () => ({ dirty: true }) },
        SAVED: [
          { target: 'failed', guard: (ctx, e) => ctx.id === e.id && ctx.conflict,
            action: (c, e) => ({ ...savedPatch(c, e), conflict: true, dirty: true, failure: c.failure ?? conflictFailure(c.id, e.revision), error: (c.failure ?? conflictFailure(c.id, e.revision)).message }) },
          { target: 'editing', guard: (ctx, e) => ctx.id === e.id && ctx.dirty, action: savedPatch },
          { target: 'saved', guard: (ctx, e) => ctx.id === e.id, action: savedPatch },
        ],
        ERROR: failed,
      } },
    saved: { on: { ...leave, ...notices, EDIT: 'editing' } },
    failed: {
      on: { ...leave, ...notices, EDIT: { action: () => ({ dirty: true }) },
        RETRY: { target: 'saving', guard: (c) => !c.conflict && c.failure?.kind !== 'deleted', action: () => ({ failure: null, error: null }) } } },
    deleted: { entry: blank, on: { ...leave, EDIT: 'editing' } },
    missing: { on: leave },
  },
});

let list: ProjectMeta[] | null = null;
const listeners = new Set<() => void>();
let listing = 0;
function refreshList() {
  const mine = ++listing;
  if (!store) { list = []; listeners.forEach((f) => f()); return; }
  void store.list().then((l) => {
    if (mine !== listing) return;
    list = l; listeners.forEach((f) => f());
  }).catch((e: unknown) => toast(errorText(e)));
}
function adopt(f: () => void) {
  adopting = true;
  try { f(); } finally { adopting = false; }
  generation = documentGeneration();
  observed = snapshot();
}

const sender = newKey();
let channel: BroadcastChannel | null = null;
function publish(type: 'saved' | 'deleted', id: string) {
  const message = { type, id, sender, at: Date.now() };
  channel?.postMessage(message);
  try { localStorage.setItem('orca.project-change', JSON.stringify(message)); } catch {  }
}
function remote(message: { type?: string; id?: string; sender?: string }) {
  if (!message || typeof message !== 'object' || message.sender === sender || !message.id) return;
  refreshList();
  if (message.id !== project.get().context.id) return;
  if (message.type === 'deleted') {
    removed.add(message.id); epoch++; opening++;
    if (hasUnsavedProject()) {
      const failure = projectFailure({ name: 'ProjectDeletedError', message: 'This project was deleted. The local draft is preserved; recover it as a copy.' }, message.id, project.get().context.revision);
      project.send({ type: 'ERROR', error: failure.message, failure }); checkpointProject();
    } else project.send({ type: 'MISSING', id: message.id });
  } else if (message.type === 'saved') {
    project.send({ type: 'REMOTE' }); checkpointProject();
  }
}

let routeVersion = 0;
async function route() {
  const mine = ++routeVersion;
  const r = currentRoute();
  routing = true;
  try {
    if (r.kind === 'projects') {
      opening++; await flushProject().catch(() => {});
      if (mine !== routeVersion) return;
      if ((project.get().value === 'opening' || project.get().value === 'missing')) project.send({ type: 'BLANK' });
      setState({ tab: 'projects', loading: null });
    }
    else if (r.kind === 'project') await openProject(r.id, { history: false });
    else if (r.kind === 'blank') {
      await flushProject();
      if (mine !== routeVersion) return;
      epoch++; opening++;
      adopt(() => { leaveDocument(); clearPlateJobs(); clearPlate(); newDocument(true); });
      project.send({ type: 'BLANK' }); setState({ tab: 'prepare' });
    } else { opening++; epoch++; await flushProject(); project.send({ type: 'MISSING', id: '' }); setState({ tab: 'prepare' }); }
  } catch (e) { toast(errorText(e)); }
  finally { if (mine === routeVersion) routing = false; }
}
let started: Promise<void> | null = null;
export function startProjects(ready: Promise<unknown> = Promise.resolve()): Promise<void> {
  if (started) return started;
  started = start(ready);
  window.orcaDesktop?.onBeforeClose?.(async () => { await started; await flushProject(); });
  return started;
}
async function start(ready: Promise<unknown>) {
  await ready.catch(() => {});
  recoveryCore = await import('./project-recovery');
  draftActivity = await import('../persist/drafts');
  const { createDraftJournal, draftSession } = await journalTools();
  Object.assign(session, draftSession(session.owner));
  journal = createDraftJournal<Doc>(session.owner, draftStorage, codec, (id) => draftActivity!.beginProjectDraftMutation(id));
  store = await projectStore();
  observed = snapshot();
  subscribeStore(() => {
    if (adopting) return;
    if (project.get().value === 'opening' || project.get().value === 'missing' || project.get().context.loadError) {
      if (getState().tab === 'projects' && !routing) showProjects();
      return;
    }
    const doc = snapshot();
    const before = observed; observed = doc;
    const empty = !doc.project && !doc.objects.length && !doc.projectPlates.some((p) => p.objects.length);
    const uneditedBlank = empty && (project.get().value === 'blank' || project.get().value === 'deleted') &&
      JSON.stringify({ ...before, selected: null }) === JSON.stringify({ ...doc, selected: null });
    if (!sameDoc(before, doc) && !uneditedBlank) {
      checkpointProject(doc);
      project.send({ type: 'EDIT' });
      if (getState().tab !== 'projects' && !routing) writeRoute(`/p/${project.get().context.id}`, !project.get().context.savedAt);
    }
    if (!routing) {
      const r = currentRoute(), tab = getState().tab;
      if (tab === 'projects' && r.kind !== 'projects') writeRoute('/projects');
      else if (tab !== 'projects' && r.kind === 'projects')
        writeRoute(project.get().value === 'blank' || project.get().value === 'deleted' ? '/' : `/p/${project.get().context.id}`);
    }
  });
  onLeaveDocument(() => {
    if (hasUnsavedProject()) checkpointProject();
    project.send({ type: 'FLUSH' });
  });
  subscribeHistory(() => {
    if (adopting || documentGeneration() === generation) return;
    if (hasUnsavedProject() && observed) checkpointProject(observed);
    generation = documentGeneration(); epoch++; opening++;
    previous = project.get().context.id;
    observed = snapshot();
    project.send({ type: 'BLANK' });
    if (!routing) writeRoute('/');
    if (observed.objects.length || observed.project) {
      checkpointProject(observed);
      project.send({ type: 'EDIT' });
      if (!routing) writeRoute(`/p/${project.get().context.id}`, true);
    }
  });
  const flush = () => { void flushProject().catch(() => {}); };
  window.addEventListener('pagehide', flush);
  window.addEventListener('beforeunload', (e) => {
    if (window.orcaDesktop) return;
    const v = project.get().value;
    if (project.get().context.loadError || (v !== 'editing' && v !== 'saving' && v !== 'failed')) return;
    flush(); e.preventDefault(); e.returnValue = '';
  });
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') flush(); });
  window.addEventListener('popstate', () => void route());
  window.addEventListener('hashchange', () => void route());
  window.addEventListener('storage', (e) => {
    if (e.key !== 'orca.project-change' || !e.newValue) return;
    try { remote(JSON.parse(e.newValue)); } catch {  }
  });
  if (typeof BroadcastChannel !== 'undefined') {
    channel = new BroadcastChannel('orca.projects'); channel.onmessage = (e) => remote(e.data);
  }
  refreshList(); void refreshProjectDrafts();
  if (currentRoute().kind === 'blank' && observed && (observed.objects.length || observed.project)) {
    checkpointProject(observed);
    project.send({ type: 'EDIT' }); writeRoute(`/p/${project.get().context.id}`, true);
  } else await route();
  if (!store) project.send({ type: 'ERROR', error: projectStorageError() });
  (await import('./boot')).startFlows();
}

export const storing = () => !!store;
export const previousProject = () => previous;
export async function flushProjectDraft(): Promise<void> {
  const id = project.get().context.id;
  if (hasUnsavedProject()) await checkpointProject().durable;
  await journal.flush(id);
}
export async function flushProject(): Promise<void> {
  const initial = project.get();
  if (initial.context.conflict || initial.value === 'failed') {
    if (hasUnsavedProject()) await checkpointProject().durable.catch(() => {});
    throw failureError(initial.context.failure ?? projectFailure(initial.context.error ?? 'Project not saved', initial.context.id, initial.context.revision));
  }
  const id = project.get().context.id;
  do {
    project.send({ type: 'FLUSH' });
    await queue;
    if (project.get().context.id !== id) return;
    if (project.get().value === 'failed' || project.get().context.conflict) throw failureError(project.get().context.failure ?? projectFailure(project.get().context.error ?? 'Project not saved', id, project.get().context.revision));
  } while (project.get().value === 'editing' || project.get().value === 'saving');
}
export async function retryProject() {
  try {
    if (!store) store = await projectStore(true);
    if (!store) throw new Error(projectStorageError());
    const r = currentRoute();
    if (project.get().context.loadError && r.kind === 'project') return await openProject(r.id, { reload: true });
    known.clear(); project.send({ type: 'RETRY' }); await flushProject();
  } catch (e) {
    const c = project.get().context, failure = projectFailure(e, c.id, c.revision);
    project.send({ type: 'ERROR', error: failure.message, failure });
  }
}
export function showProjects() { writeRoute('/projects'); void route(); }
export const openProject = async (id: string, options: { history?: boolean; reload?: boolean } = {}) => (await recoveryActions()).openProject(recoveryRuntime, id, options);

const recoveryActions = () => import('./project-recovery-actions');
const recoveryRuntime: import('./project-recovery-actions').RecoveryRuntime = {
  storage: () => store, removed, checkpoint: () => checkpointProject(), preserveSnapshot: preserveProjectSnapshot,
  hasUnsaved: hasUnsavedProject, waitForWrites: () => queue, beginOpen: () => ++opening,
  invalidateWrites: () => { epoch++; }, isCurrent: (mine) => mine === opening, adopt,
  previous: (id) => { previous = id; }, restored: () => restoredDraft, setRestored: (draft) => { restoredDraft = draft; },
  resetCaptured: () => { captured = null; }, checkpointRecovered: (id, revision, failure) => checkpointProject(snapshot(), id, revision, failure),
  drafts: draftStorage, refreshDrafts: refreshProjectDrafts, known, session, publish, refreshList, activeWrite,
};
export const keepProjectVersion = async () => (await recoveryActions()).keepProjectVersion(recoveryRuntime);
export const reloadProject = async () => (await recoveryActions()).reloadProject(recoveryRuntime);
export const recoverProjectDraft = async (id: string, options: { asCopy?: boolean } = {}) => (await recoveryActions()).recoverProjectDraft(recoveryRuntime, id, options);
export const discardProjectDraft = async (id: string) => (await recoveryActions()).discardProjectDraft(recoveryRuntime, id);

export const renameProject = async (id: string, name: string) => (await recoveryActions()).renameProject(recoveryRuntime, id, name);
export const duplicateProject = async (id: string): Promise<ProjectMeta | null> => (await recoveryActions()).duplicateProject(recoveryRuntime, id);
export const deleteProject = async (id: string): Promise<() => Promise<void>> => (await recoveryActions()).deleteProject(recoveryRuntime, id);

export type ProjectSnapshot = Snapshot<ProjectStateName, ProjectContext>;
export function useProject(): ProjectSnapshot;
export function useProject<T>(sel: (s: ProjectSnapshot) => T): T;
export function useProject<T>(sel?: (s: ProjectSnapshot) => T) { return useMachine(project, sel); }
export const projectList = () => list;
export const useProjects = () => useSyncExternalStore((l) => (listeners.add(l), () => void listeners.delete(l)), () => list);
(window as unknown as { __project: unknown }).__project = { machine: project, list: () => list };

export type ProjectLifecycle = { value: 'new' | 'loading' | 'ready' | 'dirty' | 'saving' | 'saved'; id: string }
  | { value: 'conflict'; id: string; dirty: boolean; failure: ProjectFailure }
  | { value: 'missing'; id: string } | { value: 'failed'; id: string; error: string; failure: ProjectFailure };
export function projectLifecycle(): ProjectLifecycle {
  const { value, context: c } = project.get();
  if (c.conflict) return { value: 'conflict', id: c.id, dirty: c.dirty || value === 'editing', failure: c.failure ?? conflictFailure(c.id, c.revision) };
  switch (value) {
    case 'blank': case 'deleted': return { value: 'new', id: c.id };
    case 'opening': return { value: 'loading', id: c.id };
    case 'editing': return { value: 'dirty', id: c.id };
    case 'saving': return { value, id: c.id };
    case 'saved': return { value: c.origin === 'loaded' ? 'ready' : 'saved', id: c.id };
    case 'missing': return { value, id: c.id };
    case 'failed': return { value, id: c.id, error: c.error ?? 'Project unavailable', failure: c.failure ?? projectFailure(c.error ?? 'Project unavailable', c.id, c.revision) };
    default: return assertNever(value);
  }
}
