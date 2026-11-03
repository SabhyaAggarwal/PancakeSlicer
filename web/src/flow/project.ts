import { useSyncExternalStore } from 'react';
import { clearPlate } from '../actions';
import { clearPlateJobs } from '../plater/plates';
import { newKey, projectStore, projectStorageError, type ProjectMeta, type ProjectStore } from '../persist/backend';
import type { StoredDoc } from '../persist/session';
import { documentGeneration, leaveDocument, newDocument, onLeaveDocument, snapshot, subscribeHistory, type Doc } from '../state/history';
import { getState, setState, subscribeStore, toast } from '../state/store';
import { assertNever, createMachine, useMachine, type Machine, type Snapshot } from './fsm';
import { currentRoute, writeRoute } from './routes';

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
}
export type ProjectEvent =
  | { type: 'EDIT' } | { type: 'FLUSH' } | { type: 'RETRY' } | { type: 'BLANK' } | { type: 'OPEN' }
  | { type: 'SAVED'; id: string; name: string; savedAt: number; revision: number }
  | { type: 'ERROR'; error: string }
  | { type: 'OPENED'; meta: ProjectMeta } | { type: 'MISSING'; id: string } | { type: 'DELETED' }
  | { type: 'REBASE'; revision: number } | { type: 'REMOTE' } | { type: 'ACKNOWLEDGE' };
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
const errorText = (e: unknown) => e instanceof Error && e.name === 'QuotaExceededError'
  ? 'Storage is full. Delete old projects or export this project, then retry.'
  : e instanceof Error ? e.message : String(e);
const blank = (): ProjectContext => ({ id: newKey(), name: 'Untitled', savedAt: 0, revision: 0, error: null, dirty: false, conflict: false, loadError: false, origin: 'new' });
const activeWrite = (id: string, gen: number, version: number) =>
  id === project.get().context.id && gen === documentGeneration() && version === epoch;

function save(id: string, m: Machine<ProjectStateName, ProjectEvent, ProjectContext>) {
  const doc = snapshot(), gen = documentGeneration(), version = epoch, revision = m.get().context.revision;
  const storage = store;
  queue = queue.then(async () => {
    if (!storage) throw new Error(projectStorageError());
    if (removed.has(id)) return;
    const { encode, nameOf, plateCount, thumbnail } = await codec();
    const { stored, blobs } = encode(doc);
    for (const [k, b] of blobs) {
      if (known.has(`${id}/${k}`)) continue;
      if (!(await storage.hasBlob(id, k))) await storage.putBlob(id, k, b);
      known.add(`${id}/${k}`);
    }
    if (removed.has(id)) return;
    const meta = { name: nameOf(doc), savedAt: Date.now(), objects: plateCount(doc), revision };
    await storage.save(id, meta, stored, activeWrite(id, gen, version) ? thumbnail() ?? undefined : undefined);
    if (activeWrite(id, gen, version)) m.send({ type: 'SAVED', id, ...meta, revision: revision + 1 });
    publish('saved', id);
    refreshList();
  }).catch((e: unknown) => {
    if (activeWrite(id, gen, version)) m.send({ type: 'ERROR', error: errorText(e) });
  });
}

const leave = { ERROR: { target: 'failed', action: (_: ProjectContext, e: { error: string }) => ({ error: e.error }) }, BLANK: 'blank', OPEN: 'opening', DELETED: 'deleted',
  MISSING: { target: 'missing', action: (_: ProjectContext, e: { id: string }) => ({ id: e.id, dirty: false }) } } as const;
const notices = {
  REMOTE: { action: () => ({ conflict: true }) },
  ACKNOWLEDGE: { action: () => ({ conflict: false }) },
  REBASE: { target: 'editing', action: (_: ProjectContext, e: { revision: number }) => ({ revision: e.revision, conflict: false, error: null }) },
} as const;
export const project = createMachine<ProjectStateName, ProjectEvent, ProjectContext>({
  id: 'project', initial: 'blank', context: blank(),
  states: {
    blank: { note: 'has an ID, not stored', entry: blank,
      on: { ...leave, ...notices, EDIT: 'saving' } },
    opening: { on: { ...leave,
      OPENED: { target: 'saved', action: (_, e) => ({ ...e.meta, revision: e.meta.revision ?? 0, origin: 'loaded', error: null, dirty: false, conflict: false, loadError: false }) },
      ERROR: { target: 'failed', action: (_, e) => ({ error: e.error, loadError: true }) },
    } },
    editing: { note: 'unsaved; debounce resets only on document edits',
      after: { ms: AUTOSAVE_MS, target: 'saving' },
      on: { ...leave, ...notices, EDIT: 'editing', FLUSH: 'saving' } },
    saving: { entry: (ctx, _e, m) => save(ctx.id, m),
      on: { ...leave, ...notices, EDIT: { action: () => ({ dirty: true }) },
        SAVED: [
          { target: 'editing', guard: (ctx, e) => ctx.id === e.id && ctx.dirty,
            action: (_, e) => ({ name: e.name, savedAt: e.savedAt, revision: e.revision, origin: 'saved', dirty: false, error: null }) },
          { target: 'saved', guard: (ctx, e) => ctx.id === e.id,
            action: (_, e) => ({ name: e.name, savedAt: e.savedAt, revision: e.revision, origin: 'saved', error: null, dirty: false }) },
        ],
        ERROR: { target: 'failed', action: (_, e) => ({ error: e.error, dirty: false }) },
      } },
    saved: { on: { ...leave, ...notices, EDIT: 'editing' } },
    failed: { note: 'keeps unsaved document; explicit retry',
      on: { ...leave, ...notices, EDIT: 'editing', RETRY: 'saving', FLUSH: 'saving' } },
    deleted: { entry: blank, on: { ...leave, EDIT: 'editing' } },
    missing: { note: 'no document can be edited or saved', on: leave },
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
    project.send({ type: 'MISSING', id: message.id });
  } else if (message.type === 'saved') project.send({ type: 'REMOTE' });
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
  onLeaveDocument(() => project.send({ type: 'FLUSH' }));
  subscribeHistory(() => {
    if (adopting || documentGeneration() === generation) return;
    generation = documentGeneration(); epoch++; opening++;
    previous = project.get().context.id;
    observed = snapshot();
    project.send({ type: 'BLANK' });
    if (!routing) writeRoute('/');
    if (observed.objects.length || observed.project) {
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
  refreshList();
  if (currentRoute().kind === 'blank' && observed && (observed.objects.length || observed.project)) {
    project.send({ type: 'EDIT' }); writeRoute(`/p/${project.get().context.id}`, true);
  } else await route();
  if (!store) project.send({ type: 'ERROR', error: projectStorageError() });
  (await import('./boot')).startFlows();
}

export const storing = () => !!store;
export const previousProject = () => previous;
export async function flushProject(): Promise<void> {
  if (project.get().context.loadError) return;
  const id = project.get().context.id;
  do {
    project.send({ type: 'FLUSH' });
    await queue;
    if (project.get().context.id !== id) return;
    if (project.get().value === 'failed') throw new Error(project.get().context.error ?? 'Project not saved');
  } while (project.get().value === 'editing' || project.get().value === 'saving');
}
export async function retryProject() {
  try {
    if (!store) store = await projectStore(true);
    if (!store) throw new Error(projectStorageError());
    const r = currentRoute();
    if (project.get().context.loadError && r.kind === 'project') return await openProject(r.id, { reload: true });
    await store.emptyTrash?.(); known.clear(); await flushProject();
  } catch (e) { project.send({ type: 'ERROR', error: errorText(e) }); }
}
export function showProjects() { writeRoute('/projects'); void route(); }
export async function openProject(id: string, options: { history?: boolean; reload?: boolean } = {}) {
  if (!options.reload && project.get().context.id === id && project.get().value !== 'missing' && project.get().value !== 'opening') {
    setState({ tab: 'prepare' }); if (options.history !== false) writeRoute(`/p/${id}`); return;
  }
  const mine = ++opening;
  if (!options.reload) await flushProject();
  else { epoch++; await queue; }
  if (mine !== opening) return;
  epoch++;
  project.send({ type: 'OPEN' }); setState({ loading: 'Opening', tab: 'prepare' });
  if (options.history !== false) writeRoute(`/p/${encodeURIComponent(id)}`);
  try {
    if (!store) throw new Error(projectStorageError());
    const rec = await store.load(id);
    if (mine !== opening) return;
    if (!rec || removed.has(rec.meta.id)) { project.send({ type: 'MISSING', id }); setState({ loading: null }); return; }
    const fields = await (await codec()).decode(rec.doc as StoredDoc, (k) => store!.getBlob(rec.meta.id, k));
    if (mine !== opening) return;
    if (removed.has(rec.meta.id)) { project.send({ type: 'MISSING', id }); setState({ loading: null }); return; }
    for (const k of Object.keys(rec.doc.files ?? {})) known.add(`${rec.meta.id}/${k}`);
    previous = project.get().context.id;
    adopt(() => {
      leaveDocument(); clearPlateJobs();
      setState({ ...fields, projectName: rec.meta.name, tab: 'prepare', gizmo: 'none', dialog: null, loading: null,
        job: { state: 'idle', pct: 0, stage: '' } });
      newDocument(true);
    });
    project.send({ type: 'OPENED', meta: rec.meta });
    writeRoute(`/p/${rec.meta.id}`, true);
  } catch (e) {
    if (mine !== opening) return;
    setState({ loading: null }); project.send({ type: 'ERROR', error: errorText(e) });
  }
}
export async function keepProjectVersion() {
  try {
    await queue;
    const id = project.get().context.id, rec = await store?.load(id);
    if (!rec || removed.has(rec.meta.id)) { project.send({ type: 'MISSING', id }); return; }
    project.send({ type: 'REBASE', revision: rec.meta.revision ?? 0 });
    await flushProject();
  } catch (e) { toast(errorText(e)); }
}
export const reloadProject = () => openProject(project.get().context.id, { reload: true });
export async function renameProject(id: string, name: string) {
  if (!name.trim()) return;
  if (id === project.get().context.id) { setState({ projectName: name.trim() }); return; }
  await queue;
  await store?.rename(id, name.trim()); publish('saved', id); refreshList();
}
export async function duplicateProject(id: string): Promise<ProjectMeta | null> {
  if (!store) return null;
  if (id === project.get().context.id) await flushProject();
  await queue;
  const name = list?.find((p) => p.id === id)?.name ?? project.get().context.name;
  const meta = await store.duplicate(id, newKey(), `${name} copy`);
  publish('saved', meta.id); refreshList(); return meta;
}
export async function deleteProject(id: string): Promise<() => Promise<void>> {
  const storage = store;
  if (!storage) return async () => {};
  await queue;
  await storage.remove(id);
  removed.add(id);
  if (id === project.get().context.id) {
    epoch++; opening++; project.send({ type: 'DELETED' });
    adopt(() => { clearPlateJobs(); clearPlate(); newDocument(true); });
    setState({ tab: 'projects' }); writeRoute('/projects');
  }
  publish('deleted', id); refreshList();
  return async () => { await storage.restore(id); removed.delete(id); publish('saved', id); refreshList(); };
}
export type ProjectSnapshot = Snapshot<ProjectStateName, ProjectContext>;
export function useProject(): ProjectSnapshot;
export function useProject<T>(sel: (s: ProjectSnapshot) => T): T;
export function useProject<T>(sel?: (s: ProjectSnapshot) => T) { return useMachine(project, sel); }
export const projectList = () => list;
export const useProjects = () => useSyncExternalStore((l) => (listeners.add(l), () => void listeners.delete(l)), () => list);
(window as unknown as { __project: unknown }).__project = { machine: project, list: () => list };

export type ProjectLifecycle = { value: 'new' | 'loading' | 'ready' | 'dirty' | 'saving' | 'saved'; id: string }
  | { value: 'conflict'; id: string; dirty: boolean }
  | { value: 'missing'; id: string } | { value: 'failed'; id: string; error: string };
export function projectLifecycle(): ProjectLifecycle {
  const { value, context: c } = project.get();
  if (c.conflict) return { value: 'conflict', id: c.id, dirty: c.dirty || value === 'editing' };
  switch (value) {
    case 'blank': case 'deleted': return { value: 'new', id: c.id };
    case 'opening': return { value: 'loading', id: c.id };
    case 'editing': return { value: 'dirty', id: c.id };
    case 'saving': return { value, id: c.id };
    case 'saved': return { value: c.origin === 'loaded' ? 'ready' : 'saved', id: c.id };
    case 'missing': return { value, id: c.id };
    case 'failed': return { value, id: c.id, error: c.error ?? 'Project unavailable' };
    default: return assertNever(value);
  }
}
