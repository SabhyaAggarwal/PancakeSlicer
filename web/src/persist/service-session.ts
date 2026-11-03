import { createServiceDocument } from './service-document';
const session = createServiceDocument('/api/session', 'PATCH');
export const serviceSessionSnapshot = session.snapshot;
export const subscribeServiceSession = session.subscribe;
export const sessionDocument = () => session.snapshot().document;
export const loadServiceSession = session.hydrate;
export const patchServiceSession = session.patch;
export const flushServiceSession = session.flush;
export const reloadServiceSession = session.reload;
