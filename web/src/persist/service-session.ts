import { createServiceDocument } from './service-document';
const mutationReady = async () => (await import('./project-adoption')).ensureWorkspaceMutation();
let adoptionDocument: Record<string, unknown> | null = null;
let adopting: Promise<void> | null = null;
const session = createServiceDocument('/api/session', 'PATCH', async document => {
  if (document !== adoptionDocument) await mutationReady();
});
export const serviceSessionSnapshot = session.snapshot;
export const subscribeServiceSession = session.subscribe;
export const sessionDocument = () => session.snapshot().document;
export const loadServiceSession = session.hydrate;
export async function patchServiceSession(document: Record<string, unknown>): Promise<void> {
  const captured = structuredClone(document);
  await mutationReady();
  session.patch(captured);
}
export const flushServiceSession = session.flush;
export const reloadServiceSession = session.reload;
export function adoptServiceSessionFields(document: Record<string, unknown>): Promise<void> {
  if (adopting) return adopting;
  const fields = structuredClone(document);
  adopting = (async () => {
    if (adoptionDocument) { await session.flush(true); adoptionDocument = null; }
    if (!Object.keys(fields).length) return;
    if (Object.keys(fields).some(key => key !== 'fsmRecovery' && key !== 'projectDraftOwners'))
      throw new Error('Invalid legacy session fields');
    await session.hydrate();
    if (session.snapshot().state !== 'ready') throw new Error('Pending session changes must be resolved before source adoption.');
    session.patch(fields);
    adoptionDocument = session.snapshot().document;
    await session.flush(true);
    adoptionDocument = null;
  })().finally(() => { adopting = null; });
  return adopting;
}
