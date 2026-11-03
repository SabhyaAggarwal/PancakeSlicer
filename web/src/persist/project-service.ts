import { newKey, type ProjectMeta, type ProjectStore, type StoredDocBase } from './backend';
import { jsonWrite, routeKey, serviceRequest } from './service';
import { captureProjectWire, prepareOnce, projectJson as serviceJson, projectWrite } from './project-wire';
interface ProjectRecord { meta: ProjectMeta; doc: StoredDocBase; jobReferences?: unknown }
const projectPath = (id: string) => `/api/projects/${routeKey(id)}`;
const pendingSaves = new Map<string, Promise<{ request: RequestInit; savedAt: number }>>();
async function required<T>(path: string, init?: RequestInit): Promise<T> {
  const value = await serviceJson<T>(path, init);
  if (value === null) throw new Error('The service returned no project data');
  return value;
}
const metadata = async (id: string, archived = false) => {
  if (!archived) {
    const record = await serviceJson<ProjectRecord>(projectPath(id), undefined, true);
    if (!record) throw new Error('Project not found');
    return record.meta;
  }
  return (await required<ProjectRecord>(projectPath(id) + '?archived=1')).meta;
};
export const serviceStore: ProjectStore = {
  kind: 'service',
  list: () => required<ProjectMeta[]>('/api/projects'),
  load: (id) => serviceJson<ProjectRecord>(projectPath(id), undefined, true),
  async save(id, meta, doc, thumbnail) {
    const capture = await captureProjectWire({ doc, name: meta.name, objects: meta.objects });
    const key = `${id}/${meta.revision ?? 0}/${capture.digest}`;
    const preparation = prepareOnce(pendingSaves, key, async () => {
      const previous = (meta.revision ?? 0) > 0 ? await required<ProjectRecord>(projectPath(id)) : null;
      if (previous && (previous.meta.revision ?? 0) !== meta.revision) throw Object.assign(new Error('This project changed. Reload or reapply your draft.'), { name: 'ProjectConflictError' });
      const record = { ...previous, meta: { ...previous?.meta, ...meta, id, thumbnail: thumbnail === undefined ? previous?.meta.thumbnail ?? null : thumbnail }, doc: null };
      return { request: projectWrite(await capture.project({ operationId: newKey(), expectedRevision: meta.revision ?? 0, record })), savedAt: meta.savedAt };
    });
    let pending: Awaited<typeof preparation>;
    try { pending = await preparation; } finally { capture.release(); }
    const ack = await required<{revision:number}>(projectPath(id), pending.request);
    if (ack.revision !== (meta.revision ?? 0) + 1) throw new Error('Invalid project revision acknowledgement');
    if (pendingSaves.get(key) === preparation) pendingSaves.delete(key);
    return { revision: ack.revision, savedAt: pending.savedAt };
  },
  async rename(id, name) {
    const meta = await metadata(id);
    await required(projectPath(id) + '/rename', jsonWrite('POST', { operationId: newKey(), expectedRevision: meta.revision ?? 0, name }));
  },
  async duplicate(id, newId, name) {
    const meta = await metadata(id);
    return required<ProjectMeta>(projectPath(id) + '/duplicate', jsonWrite('POST', {
      operationId: newKey(), expectedRevision: meta.revision ?? 0, newId, name,
    }));
  },
  async remove(id) {
    const meta = await metadata(id);
    await required(projectPath(id) + '/archive', jsonWrite('POST', { operationId: newKey(), expectedRevision: meta.revision ?? 0 }));
  },
  async restore(id) {
    const meta = await metadata(id, true);
    await required(projectPath(id) + '/restore', jsonWrite('POST', { operationId: newKey(), expectedRevision: meta.revision ?? 0 }));
  },
  async referenceDocuments() {
    const [live, trash] = await Promise.all([required<ProjectMeta[]>('/api/projects'), required<ProjectMeta[]>('/api/projects?archived=1')]);
    const documents = await Promise.all([...live.map((meta) => ({ meta, trashed: false })), ...trash.map((meta) => ({ meta, trashed: true }))]
      .map(async ({ meta, trashed }) => {
        const record = await required<ProjectRecord>(projectPath(meta.id) + (trashed ? '?archived=1' : ''));
        return { id: meta.id, name: meta.name, doc: record.doc, trashed };
      }));
    return { complete: true, documents };
  },
  async putBlob(_id, key, data) { await serviceRequest(`/api/assets/${routeKey(key)}`, { method: 'PUT', body: data }); },
  async getBlob(_id, key) { const response = await serviceRequest(`/api/assets/${routeKey(key)}`, undefined, true); return response && response.blob(); },
  async hasBlob(_id, key) { return (await serviceRequest(`/api/assets/${routeKey(key)}`, { method: 'HEAD' }, true)) !== null; },
};
