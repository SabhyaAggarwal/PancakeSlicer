import type { PresetType } from '../api/types';
import { projectStore, projectReferenceWritePending, subscribeProjectStorageChanges, type ProfileReferenceDocuments, type StoredDocBase } from '../persist/backend';
import { getState, subscribeStore, type State } from '../state/store';
import type { ProfileAccess } from './bulk';
import { findProfile, profileKey, profileReferenceInventory, type ProfileKey, type ProfileReference, type ProfileReferenceSource } from './identity';

export interface ProfileReferenceSnapshot {
  status: 'loading' | 'ready' | 'incomplete' | 'error';
  referencesComplete: boolean;
  references: ReadonlyMap<ProfileKey, readonly ProfileReference[]>;
  error: string | null;
}
export interface ProfileReferenceDefaults { complete: boolean; machine: readonly string[]; process: readonly string[]; filament: readonly string[] }
type ReferenceDoc = StoredDocBase & { printer?: string; process?: string; filamentSlots?: string[] };

export function buildProfileReferenceSource(s: State, projects: ProfileReferenceDocuments, defaults: ProfileReferenceDefaults): ProfileReferenceSource {
  const references: ProfileReferenceSource['references'][number][] = [];
  let complete = projects.complete && defaults.complete;
  const keys = s.userPresets.map(profileKey);
  if (new Set(keys).size !== keys.length) complete = false;
  for (const p of s.userPresets) if ((p.parentId || p.parent) && !findProfile(s, p.type, p.parentId ?? p.parent!)) complete = false;
  const add = (type: PresetType, key: string, reference: ProfileReference) => {
    if (!key) return;
    let profile = findProfile(s, type, key);
    if (!profile) {
      const wrappers = s.userPresets.filter((p) => p.type === type && (p.base?.name ?? p.inherits) === key && !p.parent && !p.parentId && !Object.keys(p.overlay ?? p.overrides).length);
      if (wrappers.length === 1) profile = wrappers[0];
    }
    if (!profile) { complete = false; return; }
    references.push({ type, key: profileKey(profile), reference });
  };
  for (const type of ['machine', 'process', 'filament'] as const)
    for (const key of defaults[type]) add(type, key, { kind: 'default', label: type === 'machine' ? 'Default printer' : type === 'process' ? 'Default process' : 'Default filament' });
  for (const project of projects.documents) {
    const doc = project.doc as ReferenceDoc;
    if (doc.printer !== undefined && typeof doc.printer !== 'string' || doc.process !== undefined && typeof doc.process !== 'string'
      || doc.filamentSlots !== undefined && (!Array.isArray(doc.filamentSlots) || doc.filamentSlots.some((key) => typeof key !== 'string'))) { complete = false; continue; }
    const reference: ProfileReference = { kind: 'project', label: `${project.name}${project.trashed ? ' (Trash)' : ''}` };
    add('machine', doc.printer ?? '', reference); add('process', doc.process ?? '', reference);
    for (const key of doc.filamentSlots ?? []) add('filament', key, reference);
  }
  return { complete, references };
}

function readDefaults(): ProfileReferenceDefaults {
  try { return { complete: true, machine: [localStorage.getItem('orca.printer') ?? ''], process: [], filament: [] }; }
  catch { return { complete: false, machine: [], process: [], filament: [] }; }
}
const listeners = new Set<() => void>();
let snapshot: ProfileReferenceSnapshot = { status: 'loading', referencesComplete: false, references: new Map(), error: null };
let documents: ProfileReferenceDocuments = { complete: false, documents: [] }, defaults = readDefaults();
let started = false, generation = 0;
const notify = () => listeners.forEach((listener) => listener());
function publish(s: State, status: ProfileReferenceSnapshot['status'], error: string | null = null) {
  const source = buildProfileReferenceSource(s, documents, defaults);
  const references = new Map<ProfileKey, readonly ProfileReference[]>();
  for (const p of s.userPresets) references.set(profileKey(p), profileReferenceInventory(s, p.type, profileKey(p), source).references);
  const complete = status === 'ready' && source.complete;
  snapshot = { status: status === 'ready' && !complete ? 'incomplete' : status, referencesComplete: complete, references,
    error: error ?? (!complete && status === 'ready' ? 'Some saved profile references could not be resolved. Refresh or recover those projects before deleting profiles.' : null) };
  notify();
}

export async function refreshProfileReferences(): Promise<ProfileReferenceSnapshot> {
  const mine = ++generation;
  defaults = readDefaults(); publish(getState(), 'loading');
  try {
    const storage = await projectStore();
    if (!storage) throw new Error('Project storage is unavailable. Profile deletion is disabled.');
    let loaded: ProfileReferenceDocuments;
    if (storage.referenceDocuments) loaded = await storage.referenceDocuments();
    else {
      const list = await storage.list();
      const rows = await Promise.all(list.map((p) => storage.load(p.id)));
      loaded = { complete: false, documents: rows.flatMap((p) => p ? [{ id: p.meta.id, name: p.meta.name, doc: p.doc }] : []) };
    }
    if (mine !== generation) return snapshot;
    documents = projectReferenceWritePending() ? { ...loaded, complete: false } : loaded; defaults = readDefaults(); publish(getState(), 'ready');
  } catch (error) {
    if (mine === generation) { documents = { ...documents, complete: false }; publish(getState(), 'error', error instanceof Error ? error.message : String(error)); }
  }
  return snapshot;
}
function start() {
  if (started) return;
  started = true;
  let prior = getState();
  subscribeStore(() => {
    const s = getState();
    if (s.userPresets !== prior.userPresets || s.printer !== prior.printer || s.process !== prior.process || s.filamentSlots !== prior.filamentSlots
      || s.projectName !== prior.projectName || s.project !== prior.project) publish(s, snapshot.status, snapshot.error);
    prior = s;
  });
  subscribeProjectStorageChanges(() => { void refreshProfileReferences(); });
  if (typeof window !== 'undefined') {
    window.addEventListener('storage', (event) => { if (event.key === 'orca.project-change' || event.key === 'orca.printer' || event.key?.startsWith('orca.profile')) void refreshProfileReferences(); });
    window.addEventListener('focus', () => { void refreshProfileReferences(); });
    if (typeof BroadcastChannel !== 'undefined') {
      const channel = new BroadcastChannel('orca.projects');
      channel.addEventListener('message', () => { void refreshProfileReferences(); });
    }
  }
  void refreshProfileReferences();
}
export const profileReferenceSnapshot = (): ProfileReferenceSnapshot => snapshot;
export function subscribeProfileReferences(listener: () => void): () => void { listeners.add(listener); start(); return () => { listeners.delete(listener); }; }
export function profileAccess(type: PresetType): ProfileAccess {
  start();
  const s = getState(), current = snapshot;
  return { referencesComplete: current.referencesComplete,
    label: (key) => findProfile(s, type, key)?.name ?? key,
    isInUse: (key) => current.references.get(findProfile(s, type, key) ? profileKey(findProfile(s, type, key)!) : key) ?? [],
    isDeletable: (key) => !!findProfile(s, type, key),
  };
}
