import { newKey, type ProjectMeta, type ProjectStore, type StoredDocBase, type ProjectCommandAck, type ProjectCommandStatus } from './backend';
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
type Command = { id: string; kind: ProjectCommandStatus['kind']; name?: string; newId?: string; expectedRevision?: number };
interface PendingCommand {
  command: Readonly<Command>; status: ProjectCommandStatus; request: RequestInit | null;
  running: Promise<ProjectCommandAck | ProjectMeta> | null; result: ProjectCommandAck | ProjectMeta | null;
}
const commands = new Map<string, PendingCommand>(), commandListeners = new Set<() => void>();
let commandSnapshot: readonly Readonly<ProjectCommandStatus>[] = [];
function publishCommands() {
  commandSnapshot = Object.freeze([...commands.values()].map((entry) => Object.freeze({ ...entry.status })));
  commandListeners.forEach((listener) => listener());
}
function executeCommand(entry: PendingCommand): Promise<ProjectCommandAck | ProjectMeta> {
  if (entry.result) return Promise.resolve(entry.result);
  if (entry.running) return entry.running;
  const task = Promise.resolve().then(async () => {
    for (const older of commands.values()) {
      if (older === entry) break;
      if (older.command.id === entry.command.id) await executeCommand(older);
    }
    if (!entry.request) {
      const { id, kind, name, newId, expectedRevision } = entry.command;
      const revision = expectedRevision ?? (await metadata(id, kind === 'restore')).revision ?? 0;
      if (!Number.isSafeInteger(revision) || revision < 0) throw new Error('Invalid project command revision');
      entry.status.expectedRevision = revision;
      entry.request = Object.freeze(jsonWrite('POST', { operationId: entry.status.operationId, expectedRevision: revision,
        ...(name !== undefined ? { name } : {}), ...(newId !== undefined ? { newId } : {}) }));
    }
    entry.status.state = 'sending'; entry.status.error = null; publishCommands();
    const ack = await required<ProjectCommandAck | ProjectMeta>(projectPath(entry.command.id) + '/' + entry.command.kind, entry.request);
    if (entry.command.kind === 'duplicate') {
      if (!('id' in ack) || ack.id !== entry.command.newId || ack.revision !== 1) throw new Error('Invalid duplicate acknowledgement');
    } else if (!('operationId' in ack) || ack.operationId !== entry.status.operationId || ack.revision !== entry.status.expectedRevision! + 1)
      throw new Error('Invalid project command acknowledgement');
    entry.result = ack; entry.status.state = 'acknowledged'; publishCommands();
    return ack;
  }).catch((error: unknown) => {
    entry.status.state = (error as { status?: number })?.status === 409 ? 'conflict' : 'failed';
    entry.status.error = error instanceof Error ? error.message : 'Project command failed'; publishCommands(); throw error;
  });
  entry.running = task;
  void task.finally(() => { if (entry.running === task) entry.running = null; }).catch(() => {});
  return task;
}
async function projectCommand(command: Command) {
  const key = JSON.stringify(command);
  let entry = commands.get(key);
  if (!entry) {
    entry = { command: Object.freeze({ ...command }), request: null, running: null, result: null,
      status: { ...command, operationId: newKey(), expectedRevision: command.expectedRevision ?? null, state: 'preparing', error: null } };
    commands.set(key, entry); publishCommands();
  }
  const ack = await executeCommand(entry);
  if (commands.get(key) === entry) { commands.delete(key); publishCommands(); }
  return ack;
}
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
  rename: async (id, name) => await projectCommand({ id, kind: 'rename', name }) as ProjectCommandAck,
  duplicate: async (id, newId, name) => await projectCommand({ id, kind: 'duplicate', newId, name }) as ProjectMeta,
  remove: async (id) => await projectCommand({ id, kind: 'archive' }) as ProjectCommandAck,
  restore: async (id, expectedRevision) => await projectCommand({ id, kind: 'restore', ...(expectedRevision !== undefined ? { expectedRevision } : {}) }) as ProjectCommandAck,
  commands: () => commandSnapshot,
  subscribeCommands(listener) { commandListeners.add(listener); return () => { commandListeners.delete(listener); }; },
  discardRejectedCommand(operationId) {
    const found = [...commands].find(([, entry]) => entry.status.operationId === operationId);
    if (!found || found[1].status.state !== 'conflict' || found[1].running) throw new Error('Only an explicitly rejected conflict can be discarded. Retry ambiguous commands unchanged.');
    commands.delete(found[0]); publishCommands();
  },
  async referenceDocuments() {
    const [live, trash] = await Promise.all([required<ProjectMeta[]>('/api/projects'), required<ProjectMeta[]>('/api/projects?archived=1')]);
    const documents = await Promise.all([...live.map((meta) => ({ meta, trashed: false })), ...trash.map((meta) => ({ meta, trashed: true }))]
      .map(async ({ meta, trashed }) => {
        const record = await required<ProjectRecord>(projectPath(meta.id) + (trashed ? '?archived=1' : ''));
        return { id: meta.id, name: meta.name, doc: record.doc, trashed };
      }));
    const adoption = (await import('./project-adoption')).projectAdoptionSnapshot();
    return { complete: adoption.state === 'ready' && adoption.coverage === 'complete', documents };
  },
  async putBlob(_id, key, data) { await serviceRequest(`/api/assets/${routeKey(key)}`, { method: 'PUT', body: data }); },
  async getBlob(_id, key) { const response = await serviceRequest(`/api/assets/${routeKey(key)}`, undefined, true); return response && response.blob(); },
  async hasBlob(_id, key) { return (await serviceRequest(`/api/assets/${routeKey(key)}`, { method: 'HEAD' }, true)) !== null; },
};
