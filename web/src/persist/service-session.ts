import { createServiceDocument } from './service-document';
import { requireWorkspaceService } from './service';
const owners = (remote: unknown, local: unknown) => [...new Set([...(local as string[] ?? []), ...(remote as string[] ?? [])])].slice(0, 32);
const latest = (_remote: unknown, local: unknown) => local;
const session = createServiceDocument('/api/session', 'PATCH', requireWorkspaceService, { projectDraftOwners: owners, fsmRecovery: latest });
export const serviceSessionSnapshot = session.snapshot;
export const subscribeServiceSession = session.subscribe;
export const sessionDocument = () => session.snapshot().document;
export const loadServiceSession = session.hydrate;
export async function patchServiceSession(document: Record<string, unknown>): Promise<void> {
  const captured = structuredClone(document);
  await requireWorkspaceService();
  session.patch(captured);
}
export const flushServiceSession = session.flush;
export const reloadServiceSession = session.reload;
