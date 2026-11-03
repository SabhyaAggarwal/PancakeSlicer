import { useSyncExternalStore } from 'react';
import type { StoredDocBase } from '../persist/backend';
import { plateSignature } from '../slice-input';
import type { Doc } from '../state/history';
import { getState, setState, toast, type JobView } from '../state/store';
import type { JobStatus } from '../api/types';
import { project, flushProject, leaveProjectVersion, restoreProjectVersion, viewProjectVersion, type ProjectMeta } from '../flow/project';
import { slice } from '../flow/slice';
import { describeChange, type VersionChange } from './version-change';

export interface ProjectVersion {
  id: number; revision: number; at: number; until: number; name: string; objects: number;
  change: VersionChange[]; sealed?: boolean;
  slice?: { at: number; timeS?: number; grams?: number; layers?: number };
}
export interface HistoryState {
  open: boolean;
  projectId: string | null;
  versions: ProjectVersion[] | null;
  error: string | null;
  viewing: ProjectVersion | null;
  loading: number | null;
}

let state: HistoryState = { open: false, projectId: null, versions: null, error: null, viewing: null, loading: null };
const listeners = new Set<() => void>();
const set = (patch: Partial<HistoryState>) => { state = { ...state, ...patch }; listeners.forEach((l) => l()); };
export const historyState = () => state;
export const useHistory = () => useSyncExternalStore((l) => (listeners.add(l), () => void listeners.delete(l)), historyState);

const service = () => import('../persist/service');
const path = (id: string) => `/api/versions/${encodeURIComponent(id)}`;
async function post<T>(url: string, body: unknown): Promise<T> {
  const { serviceJson, jsonWrite } = await service();
  const value = await serviceJson<T>(url, jsonWrite('POST', body));
  if (value === null) throw new Error('The service returned no version data');
  return value;
}
const errorText = (e: unknown) => e instanceof Error ? e.message : String(e);

const saved = new Map<string, Doc>();
let override: { id: string; change: VersionChange[] } | null = null;
export const savedVersionBase = (id: string, doc: Doc) => { saved.set(id, doc); };
export function changeForSave(id: string, doc: Doc, revision: number): VersionChange[] {
  if (override?.id === id) { const { change } = override; override = null; return change; }
  const base = saved.get(id);
  return base ? describeChange(base, doc) : revision === 0 ? [{ label: 'Created' }] : [];
}

let reading = 0;
export async function refreshHistory(id = project.get().context.id) {
  const mine = ++reading;
  if (state.projectId !== id) set({ projectId: id, versions: null, error: null });
  try {
    const { serviceJson } = await service();
    const list = await serviceJson<{ revision: number; versions: ProjectVersion[] }>(path(id), undefined, true);
    if (mine === reading) set({ versions: list?.versions ?? [], error: null });
  } catch (e) {
    if (mine === reading) set({ error: errorText(e) });
  }
}
export function openHistory() {
  const { value, context } = project.get();
  if (value === 'blank' || value === 'missing') return;
  set({ open: true });
  void refreshHistory(context.id);
}
export function closeHistory() { set({ open: false }); }
export const toggleHistory = () => (state.open ? closeHistory() : openHistory());

async function showSlice(id: string, v: ProjectVersion) {
  const status = await post<JobStatus & { id: string }>(`${path(id)}/${v.id}/open-slice`, {});
  const job: JobView = { id: status.id, state: 'done', pct: 100, stage: 'done', status, plateSignature: plateSignature(getState()), warnings: [] };
  setState({ job });
}

let viewToken = 0;
export async function viewVersion(v: ProjectVersion) {
  const id = state.projectId ?? project.get().context.id, latest = state.versions?.[0];
  if (latest && v.id === latest.id && !state.viewing) return;
  if (latest && v.id === latest.id) return backToLatest();
  const mine = ++viewToken;
  set({ loading: v.id });
  try {
    const { projectJson } = await import('../persist/project-wire');
    const record = await projectJson<{ meta: ProjectMeta; doc: StoredDocBase }>(`${path(id)}/${v.id}`);
    if (!record) throw new Error('This version is no longer kept.');
    if (mine !== viewToken) return;
    if (!(await viewProjectVersion(id, record)) || mine !== viewToken) return;
    set({ viewing: v });
    if (v.slice) await showSlice(id, v).catch((e: unknown) => toast(`Slice not loaded: ${errorText(e)}`));
  } catch (e) {
    if (mine === viewToken) toast(errorText(e));
  } finally {
    if (mine === viewToken) set({ loading: null });
  }
}
export async function backToLatest() {
  const id = state.projectId;
  ++viewToken;
  set({ viewing: null, loading: null });
  if (id) await leaveProjectVersion(id);
}
export async function restoreVersion() {
  const v = state.viewing, id = state.projectId;
  if (!v || !id) return;
  override = { id, change: [{ label: `Restored ${versionTime(v.until)}` }] };
  ++viewToken;
  set({ viewing: null, loading: null });
  try {
    await restoreProjectVersion();
    await attachSlice(slice.get().context.result);
  } catch (e) {
    toast(errorText(e));
  }
  void refreshHistory(id);
}

async function sealCurrent(signature: string): Promise<{ id: string; revision: number; version: number } | null> {
  await flushProject();
  const { value, context } = project.get();
  if (state.viewing || value !== 'saved' || context.conflict || plateSignature(getState()) !== signature) return null;
  const sealed = await post<ProjectVersion>(`${path(context.id)}/seal`, { revision: context.revision });
  return { id: context.id, revision: context.revision, version: sealed.id };
}
let sealing: { signature: string; done: ReturnType<typeof sealCurrent> } | null = null;
async function attachSlice(result: ReturnType<typeof slice.get>['context']['result']) {
  if (!result?.job.id || state.viewing) return;
  const seal = sealing?.signature === result.signature ? await sealing.done.catch(() => null) : null;
  const target = seal ?? await sealCurrent(result.signature);
  if (!target || plateSignature(getState()) !== result.signature) return;
  await post(`${path(target.id)}/${target.version}/slice`, { revision: target.revision, jobId: result.job.id });
  if (state.open && state.projectId === target.id) void refreshHistory(target.id);
}

let tracking = false;
export function startVersionTracking() {
  if (tracking) return;
  tracking = true;
  let target: string | null = null, attached: string | undefined;
  slice.subscribe((snap) => {
    if (state.viewing) return;
    if (snap.value === 'slicing' && snap.context.target && snap.context.target !== target) {
      target = snap.context.target;
      const done = sealCurrent(target);
      done.catch(() => {});
      sealing = { signature: target, done };
    }
    const result = snap.context.result;
    if (snap.value === 'sliced' && result?.job.id && result.job.id !== attached) {
      attached = result.job.id;
      void attachSlice(result).catch((e: unknown) => console.warn('Version slice not kept', e));
    }
  });
  let revision = project.get().context.revision, id = project.get().context.id;
  project.subscribe((snap) => {
    if (snap.value === 'opening' && state.viewing) { ++viewToken; set({ viewing: null, loading: null }); }
    if (snap.context.id !== id) {
      id = snap.context.id;
      if (state.viewing || state.projectId !== id) set({ viewing: null, loading: null, versions: null, projectId: null, open: state.open && snap.value !== 'blank' });
      if (state.open) void refreshHistory(id);
    } else if (snap.context.revision !== revision && state.open) void refreshHistory(id);
    revision = snap.context.revision;
  });
}

const clock = (t: number) => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
export function dayOf(t: number, now = Date.now()) {
  const d = new Date(t), today = new Date(now);
  const days = Math.round((new Date(today.toDateString()).getTime() - new Date(d.toDateString()).getTime()) / 86_400_000);
  return days === 0 ? 'Today' : days === 1 ? 'Yesterday' : d.toLocaleDateString([], { weekday: days < 7 ? 'long' : undefined, month: 'short', day: 'numeric', year: d.getFullYear() === today.getFullYear() ? undefined : 'numeric' });
}
export const versionClock = clock;
export const versionTime = (t: number) => dayOf(t) === 'Today' ? clock(t) : `${dayOf(t)} ${clock(t)}`;
