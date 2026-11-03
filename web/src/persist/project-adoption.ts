import type { ProjectDraft } from './drafts';
import { captureLegacyProjects, hasLegacyProjects, legacyProjectEnvelope, type LegacyProjectInventory } from './legacy-projects';
import { requireWorkspaceService, routeKey, serviceJson, serviceRequest } from './service';
import { flushServiceSession, loadServiceSession, patchServiceSession } from './service-session';
import { documentText } from './wire';
type AdoptionReceipt = { durable: true; digest: string; operationId: string; sourceId: string; projectIds: string[]; assetKeys: string[] };
type AdoptionStatus = { state: 'loading' | 'pending' | 'ready' | 'failed'; error: string | null; receipt: AdoptionReceipt | null };
let status: AdoptionStatus = { state: 'loading', error: null, receipt: null }, adopting: Promise<void> | null = null;
let captured: Awaited<ReturnType<typeof createProjectAdoption>> | null = null;
const listeners = new Set<() => void>();
const publish = (next: AdoptionStatus) => { status = next; listeners.forEach((listener) => listener()); };
export const projectAdoptionSnapshot = () => status;
export const subscribeProjectAdoption = (listener: () => void) => { listeners.add(listener); return () => { listeners.delete(listener); }; };
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
const safeKey = (key: unknown): key is string => typeof key === 'string' && /^[A-Za-z0-9._-]{1,200}$/.test(key) && key !== '.' && key !== '..';
export async function createProjectAdoption(inventory: LegacyProjectInventory) {
  const envelope = await legacyProjectEnvelope(inventory);
  const rows = (database: string, name: string) => inventory.databases.filter((db) => db.name === database || database === 'orca-web' && db.name === 'desktop-projects')
    .flatMap((db) => db.stores.find((store) => store.name === name)?.records ?? []);
  if (rows('orca-web', 'quarantine').length) throw new Error('Quarantined projects need explicit repair before adoption. The source is retained.');
  const globalBlobs = new Map<string, unknown>();
  const assets = new Map<string, { key: string; part: string; size: number; sha256: string }>();
  function keepAsset(key: string, blob: unknown) {
    if (!safeKey(key) || !(blob instanceof Blob)) throw new Error(`Legacy model asset is missing or invalid: ${key}`);
    const part = envelope.partsByBlob.get(blob);
    if (!part) throw new Error(`Legacy asset was not captured: ${key}`);
    const previous = assets.get(key);
    if (previous && previous.sha256 !== part.sha256) throw new Error(`Conflicting legacy asset key: ${key}`);
    assets.set(key, { key, part: part.key, size: blob.size, sha256: part.sha256 });
  }
  for (const { key, value } of rows('orca-web', 'blobs')) { keepAsset(String(key), value); globalBlobs.set(String(key), value); }
  function projectRecord(value: unknown) {
    if (!object(value) || !safeKey(value.id) || typeof value.name !== 'string' || !value.name.trim() || !Number.isFinite(value.objects) || !Number.isFinite(value.savedAt) || !object(value.doc))
      throw new Error('A legacy project cannot be read losslessly. Repair or export it before adoption.');
    const { doc, blobs, ...meta } = value;
    if (meta.revision !== undefined && (!Number.isSafeInteger(meta.revision) || Number(meta.revision) < 0)) throw new Error('Invalid legacy project revision');
    const files = doc.files ?? (Array.isArray(blobs) ? Object.fromEntries(blobs.map((key) => [String(key), globalBlobs.get(String(key)) instanceof File ? (globalBlobs.get(String(key)) as File).name : ''])) : {});
    if (!object(files)) throw new Error('Invalid legacy project asset inventory');
    for (const key of Object.keys(files)) keepAsset(key, globalBlobs.get(key));
    return { meta: { ...meta, id: value.id, revision: Number(meta.revision ?? 0), thumbnail: meta.thumbnail ?? null }, doc: { ...doc, files } };
  }
  const projects = rows('orca-web', 'sessions').map(({ value }) => projectRecord(value));
  const trash = rows('orca-web', 'trash').map(({ value }) => projectRecord(value));
  const drafts = rows('orca-web-drafts', 'drafts').map(({ value }) => {
    if (!object(value) || !safeKey(value.id) || !safeKey(value.projectId) || !Number.isSafeInteger(value.sequence) || !object(value.doc) || !object(value.blobs)) throw new Error('Invalid legacy recovery draft. The source is retained.');
    const { blobs, ...document } = value as unknown as ProjectDraft;
    for (const [key, blob] of Object.entries(blobs)) keepAsset(key, blob);
    for (const key of Object.keys(document.doc.files ?? {})) keepAsset(key, blobs[key]);
    const { doc: _doc, ...metadata } = document;
    return { id: document.id, sequence: document.sequence, document, metadata, updatedAt: document.updatedAt };
  });
  const aliases = rows('orca-web', 'aliases');
  if (aliases.some(({ key, value }) => !safeKey(key) || !safeKey(value))) throw new Error('Invalid legacy project alias');
  for (const part of envelope.parts) if (![...assets.values()].some((asset) => asset.part === part.key))
    assets.set(`legacyblob-${part.sha256}`, { key: `legacyblob-${part.sha256}`, part: part.key, size: part.blob.size, sha256: part.sha256 });
  const manifest = { operationId: `adopt-${envelope.digest}`, sourceId: `browser-projects:${envelope.digest}`, digest: envelope.digest,
    projects, trash, tombstones: rows('orca-web', 'tombstones'), aliases, drafts,
    legacy: JSON.parse(envelope.manifest).inventory as unknown, legacyText: envelope.manifest, assets: [...assets.values()],
    preferences: undefined as Record<string, unknown> | undefined };
  return { ...envelope, manifest };
}
async function adopt() {
  await requireWorkspaceService();
  if (!captured) {
    const inventory = await captureLegacyProjects();
    if (!hasLegacyProjects(inventory)) { publish({ state: 'ready', error: null, receipt: null }); return; }
    captured = await createProjectAdoption(inventory);
  }
  const source = captured, { manifest } = source;
  publish({ state: 'pending', error: null, receipt: null });
  let receipt = await serviceJson<AdoptionReceipt>(`/api/projects/adoptions/${routeKey(manifest.operationId)}`, undefined, true);
  if (!receipt) {
    if (source.inventory.preferences && Object.keys(source.inventory.preferences).length && !manifest.preferences) {
      const remote = await serviceJson<{revision:number;document:Record<string, unknown>}>('/api/preferences');
      if (!remote || !object(remote.document)) throw new Error('Cannot read saved service preferences for adoption');
      const conflicts = Object.entries(source.inventory.preferences).filter(([key, value]) => Object.hasOwn(remote.document, key) && documentText(remote.document[key]) !== documentText(value));
      if (conflicts.length) throw Object.assign(new Error(`Browser and service preferences differ: ${conflicts.map(([key]) => key).join(', ')}. Both sources are retained; resolve them explicitly.`), { name: 'ProjectConflictError' });
      manifest.preferences = { ...remote.document, ...source.inventory.preferences };
    }
    const body = new FormData(); body.set('manifest', documentText(manifest));
    for (const part of source.parts) body.set(part.key, part.blob, part.name ?? part.key);
    const response = await serviceRequest('/api/projects/adopt', { method: 'POST', body });
    receipt = response && await response.json() as AdoptionReceipt;
  }
  if (!receipt || receipt.durable !== true || receipt.digest !== manifest.digest || receipt.operationId !== manifest.operationId || receipt.sourceId !== manifest.sourceId
    || !Array.isArray(receipt.projectIds) || !Array.isArray(receipt.assetKeys)
    || [...manifest.projects, ...manifest.trash].some(({ meta }) => !receipt!.projectIds.includes(String(meta.id)))
    || manifest.assets.some(({ key }) => !receipt!.assetKeys.includes(key))) throw new Error('The service has not acknowledged the complete project inventory.');
  const remote = await loadServiceSession(), patch: Record<string, unknown> = {};
  if (!remote.document.fsmRecovery && source.inventory.recovery) patch.fsmRecovery = JSON.parse(source.inventory.recovery);
  if (!remote.document.projectDraftOwners) {
    const lineage: unknown = JSON.parse(source.inventory.lineage ?? '[]');
    if (!Array.isArray(lineage) || lineage.some((item) => typeof item !== 'string')) throw new Error('Invalid legacy recovery lineage');
    patch.projectDraftOwners = [...new Set([...(source.inventory.owner ? [source.inventory.owner] : []), ...lineage])];
  }
  if (Object.keys(patch).length) { patchServiceSession(patch); await flushServiceSession(true); }
  const current = await legacyProjectEnvelope(await captureLegacyProjects());
  if (current.digest !== source.digest) {
    captured = null;
    throw new Error('Legacy projects changed during adoption. Close older Orca windows and retry; both captured versions are retained.');
  }
  publish({ state: 'ready', error: null, receipt });
}
export function ensureProjectAdoption(retry = false): Promise<void> {
  if (retry && status.state === 'failed') adopting = null;
  adopting ??= adopt().catch((error: unknown) => {
    publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), receipt: status.receipt }); throw error;
  });
  return adopting;
}
