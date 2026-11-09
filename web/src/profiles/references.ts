import { preferenceText, savedPreferencesSnapshot, subscribeSavedPreferences } from '../saved-preferences';
import { projectDraftStore, projectDraftMutationSnapshot, subscribeProjectDraftMutations, settleDraftWriters } from '../persist/drafts';
import type { PresetType } from '../api/types';
import { projectStore, projectReferenceWritePending, subscribeProjectStorageChanges, type ProfileReferenceDocuments, type StoredDocBase } from '../persist/backend';
import { getState, subscribeStore, type State } from '../state/store';
import type { ProfileAccess } from './bulk';
import { findProfile, profileKey, type ProfileKey, type ProfileReference, type ProfileReferenceSource } from './identity';

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
      if (wrappers.length > 1) { complete = false; return; }
      profile = wrappers[0];
    }
    if (!profile) return;
    references.push({ type, key: profileKey(profile), reference });
  };
  for (const type of ['machine', 'process', 'filament'] as const)
    for (const key of defaults[type]) add(type, key, { kind: 'default', label: type === 'machine' ? 'Default printer' : type === 'process' ? 'Default process' : 'Default filament' });
  for (const child of s.userPresets) {
    const rule = child.applicability, worksWith = {kind:'inheritance' as const,label:`Works with: ${child.name}`,owner:profileKey(child)};
    if (rule?.printers && 'printerIds' in rule.printers) for (const id of rule.printers.printerIds) add('machine', id, worksWith);
    for (const id of rule?.exclude ?? []) add('machine', id, worksWith);
    for (const id of rule?.prints?.profileIds ?? []) add('process', id, worksWith);
    if (rule?.filaments && 'profileIds' in rule.filaments) for (const id of rule.filaments.profileIds) add('filament', id, worksWith);
    if (child.masterId) add(child.type,child.masterId,{kind:'inheritance',label:child.name,owner:profileKey(child)});
    else if (child.kind !== 'master') {
      const masters = s.userPresets.filter(p => p.type === child.type && p.kind === 'master' && p.base?.name === child.base?.name && (!p.base?.vendor || p.base.vendor === child.base?.vendor));
      if (masters.length > 1) complete = false;
      else if (masters[0]) add(child.type,profileKey(masters[0]),{kind:'inheritance',label:child.name,owner:profileKey(child)});
    }
  }
  for (const material of s.userPresets) if (material.type === 'filament' && material.printerId)
    add('machine', material.printerId, { kind: 'inheritance', label: material.name, owner: profileKey(material) });
  for (const [ownerId, setup] of Object.entries(s.printerLibrary?.setups ?? {})) {
    const owner = findProfile(s, 'machine', ownerId);
    if (!owner || ownerId !== setup.printerId) { complete = false; continue; }
    const label = `Printer material library: ${owner.name}`;
    add('machine', ownerId, { kind: 'default', label });
    if (!Array.isArray(setup.materialIds) || !Array.isArray(setup.slots) || !setup.tuning || !setup.assignments) { complete = false; continue; }
    for (const key of new Set([...setup.materialIds, ...setup.slots.map(slot => slot.materialId), ...Object.keys(setup.tuning)])) {
      add('filament', key, { kind: 'inheritance', label, owner: ownerId });
    }
    if (Object.values(setup.assignments).some(slot => !setup.slots.some(s => s.id === slot))) complete = false;
  }
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
  return {complete:savedPreferencesSnapshot().state === 'ready', machine:[preferenceText('orca.printer') ?? ''],process:[],filament:[]};
}
const listeners = new Set<() => void>();
let snapshot: ProfileReferenceSnapshot = { status: 'loading', referencesComplete: false, references: new Map(), error: null };
let documents: ProfileReferenceDocuments = { complete: false, documents: [] }, defaults = readDefaults();
let started = false, generation = 0;
const notify = () => listeners.forEach((listener) => listener());
function publish(s: State, status: ProfileReferenceSnapshot['status'], error: string | null = null) {
  const source = buildProfileReferenceSource(s, documents, defaults);
  const references = new Map<ProfileKey, readonly ProfileReference[]>();
  const push = (key: string, reference: ProfileReference) => references.set(key, [...(references.get(key) ?? []), reference]);
  for (const p of s.userPresets) references.set(profileKey(p), []);
  for (const type of ['machine', 'process', 'filament'] as const) {
    const active = type === 'machine' ? [s.printer] : type === 'process' ? [s.process] : s.filamentSlots;
    active.forEach((key, slot) => {
      const profile = findProfile(s, type, key); if (!profile) return;
      push(profileKey(profile), {kind: 'active', label: type === 'filament' ? `Filament ${slot + 1}` : type === 'machine' ? 'Printer' : 'Process'});
      if (s.project || s.projectName) push(profileKey(profile), {kind: 'project', label: s.projectName ?? s.project?.name ?? 'Current project'});
    });
  }
  for (const child of s.userPresets) if (child.parentId || child.parent) {
    const parent = findProfile(s, child.type, child.parentId ?? child.parent!);
    if (parent) push(profileKey(parent), {kind: 'inheritance', label: child.name, owner: profileKey(child)});
  }
  for (const item of source.references) push(item.key, item.reference);
  const complete = status === 'ready' && source.complete;
  snapshot = { status: status === 'ready' && !complete ? 'incomplete' : status, referencesComplete: complete, references,
    error: error ?? (!complete && status === 'ready' ? 'Some projects link to profiles that are still syncing. Refresh or open those projects before deleting profiles.' : null) };
  notify();
}

export async function refreshProfileReferences(): Promise<ProfileReferenceSnapshot> {
  await settleDraftWriters().catch(() => {});
  const mine = ++generation, draftRevision = projectDraftMutationSnapshot().revision;
  defaults = readDefaults(); publish(getState(), 'loading');
  try {
    const storage = await projectStore();
    if (!storage) throw new Error('Can\'t reach project storage right now, so deleting profiles is temporarily paused.');
    const [stored, drafts] = await Promise.all([storage.referenceDocuments(), projectDraftStore().then((store) => store.list())]);
    const loaded: ProfileReferenceDocuments = { ...stored, documents: [...stored.documents, ...drafts.map(draft => ({ id: draft.id, name: `${draft.name} (${draft.kind === 'snapshot' ? 'Snapshot' : 'Draft'})`, doc: draft.doc }))] };
    if (mine !== generation) return snapshot;
    const draftMutation = projectDraftMutationSnapshot();
    const stale = draftMutation.revision !== draftRevision || draftMutation.pending > 0 || draftMutation.error !== null;
    documents = projectReferenceWritePending() || stale ? { ...loaded, complete: false } : loaded; defaults = readDefaults(); publish(getState(), 'ready');
  } catch (error) {
    if (mine === generation) { documents = { ...documents, complete: false }; publish(getState(), 'error', error instanceof Error ? error.message : String(error)); }
  }
  return snapshot;
}
let stale = false;
const refreshWatched = () => {
  if (listeners.size) void refreshProfileReferences();
  else if (!stale) { stale = true; ++generation; documents = { ...documents, complete: false }; publish(getState(), 'incomplete'); }
};
function start() {
  if (started) return;
  started = true;
  let prior = getState();
  subscribeStore(() => {
    const s = getState();
    if (s.printerLibrary !== prior.printerLibrary || s.userPresets !== prior.userPresets || s.printer !== prior.printer || s.process !== prior.process || s.filamentSlots !== prior.filamentSlots
      || s.projectName !== prior.projectName || s.project !== prior.project) publish(s, snapshot.status, snapshot.error);
    prior = s;
  });
  subscribeSavedPreferences(() => { defaults = readDefaults(); publish(getState(),snapshot.status,snapshot.error); });
  subscribeProjectStorageChanges(refreshWatched);
  subscribeProjectDraftMutations(() => {
    ++generation; documents = { ...documents, complete: false }; publish(getState(), 'incomplete');
    const mutation = projectDraftMutationSnapshot();
    if (!mutation.pending && !mutation.error) refreshWatched();
  });
  if (typeof window !== 'undefined') {
    window.addEventListener('storage', (event) => { if (event.key === 'orca.project-change' || event.key === 'orca.printer' || event.key?.startsWith('orca.profile')) refreshWatched(); });
    window.addEventListener('focus', refreshWatched);
    if (typeof BroadcastChannel !== 'undefined') {
      const channel = new BroadcastChannel('orca.projects');
      channel.addEventListener('message', refreshWatched);
    }
  }
  void refreshProfileReferences();
}
export const profileReferenceSnapshot = (): ProfileReferenceSnapshot => snapshot;
export function subscribeProfileReferences(listener: () => void): () => void {
  listeners.add(listener);
  if (started && stale) { stale = false; void refreshProfileReferences(); }
  start();
  return () => { listeners.delete(listener); };
}
export function profileAccess(type: PresetType): ProfileAccess {
  start();
  const s = getState(), current = snapshot;
  return { referencesComplete: current.referencesComplete,
    label: (key) => findProfile(s, type, key)?.name ?? key,
    isInUse: (key) => current.references.get(findProfile(s, type, key) ? profileKey(findProfile(s, type, key)!) : key) ?? [],
    isDeletable: (key) => !!findProfile(s, type, key),
  };
}
