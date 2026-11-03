import { projectId } from './identity';
import { jsonWrite, serviceJson } from './service';
export type JsonValue = null | boolean | number | string | readonly JsonValue[] | { readonly [key: string]: JsonValue };
export type ServiceDocument = Record<string, unknown>;
export type DocumentSnapshot<D> = { state: 'loading' | 'ready' | 'pending' | 'failed'; revision: number; document: D; error: string | null; conflict: boolean };
type KeyMerge = (remote: unknown, local: unknown) => unknown;
const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);
export function createServiceDocument<D extends ServiceDocument>(path: string, method: 'PUT' | 'PATCH', beforeWrite: (document: D) => Promise<unknown> = () => Promise.resolve(), merge: Record<string, KeyMerge> = {}) {
  let current: DocumentSnapshot<D> = { state: 'loading', revision: 0, document: {} as D, error: null, conflict: false };
  let hydrated = false, loading: Promise<DocumentSnapshot<D>> | null = null, running: Promise<void> | null = null;
  let timer: ReturnType<typeof setTimeout> | undefined, serial = 0, savedSerial = 0;
  let beforeHydration: Partial<D> = {};
  const unsaved = new Map<string, number>();
  let base = {} as D;
  let operation: { operationId: string; expectedRevision: number; document: D; serial: number } | null = null;
  const listeners = new Set<() => void>();
  const publish = (patch: Partial<DocumentSnapshot<D>>) => { current = { ...current, ...patch }; listeners.forEach((listener) => listener()); };
  const hydrate = () => {
    if (hydrated) return Promise.resolve(current);
    loading ??= serviceJson<{revision:number;document:D}>(path).then((remote) => {
      if (!remote || !Number.isSafeInteger(remote.revision) || remote.revision < 0 || !remote.document || typeof remote.document !== 'object' || Array.isArray(remote.document)) throw new Error('Invalid saved service document');
      hydrated = true; base = remote.document;
      publish({ ...remote, document: { ...remote.document, ...beforeHydration },
        state: serial > savedSerial ? 'pending' : 'ready', error: null, conflict: false });
      beforeHydration = {};
      return current;
    }).catch((error: unknown) => {
      loading = null; publish({ state: 'failed', error: error instanceof Error ? error.message : String(error) }); throw error;
    });
    return loading;
  };
  function patch(document: Partial<D>) {
    const captured = structuredClone(document);
    if (!hydrated) beforeHydration = { ...beforeHydration, ...captured };
    serial++; for (const key of Object.keys(captured)) unsaved.set(key, serial);
    publish({ document: { ...current.document, ...captured } });
    if (current.state === 'failed') return;
    publish({ state: 'pending', error: null }); clearTimeout(timer);
    timer = setTimeout(() => { void flush().catch(() => {}); }, 500);
  }
  async function flush(retry = false): Promise<void> {
    clearTimeout(timer);
    if (running) { await running; if (savedSerial < serial) return flush(retry); return; }
    if (current.state === 'failed' && !retry) throw new Error(current.error ?? 'The service document was not saved');
    running = (async () => {
      if (!hydrated) await hydrate();
      let rebases = 0;
      while (savedSerial < serial) {
        operation ??= { operationId: projectId(), expectedRevision: current.revision, document: current.document, serial };
        const captured = operation, { serial: _serial, ...body } = captured;
        await beforeWrite(captured.document);
        let ack: { revision: number } | null;
        try { ack = await serviceJson<{revision:number}>(path, jsonWrite(method, body)); }
        catch (error) {
          if ((error as { code?: string }).code !== 'revision_conflict' || ++rebases > 3) throw error;
          const remote = await serviceJson<{revision:number;document:D}>(path);
          if (!remote || !Number.isSafeInteger(remote.revision) || !remote.document || typeof remote.document !== 'object' || Array.isArray(remote.document)) throw error;
          const next: Record<string, unknown> = { ...remote.document };
          for (const key of [...unsaved.keys()]) {
            const theirs = remote.document[key], mine = current.document[key];
            if (same(theirs, base[key])) next[key] = mine;
            else if (same(theirs, mine)) unsaved.delete(key);
            else if (merge[key]) next[key] = merge[key](theirs, mine);
            else throw error;
          }
          base = remote.document; operation = null; publish({ revision: remote.revision, document: next as D });
          continue;
        }
        if (!ack || ack.revision !== captured.expectedRevision + 1) throw new Error('Invalid service revision acknowledgement');
        for (const [key, at] of unsaved) if (at <= captured.serial) unsaved.delete(key);
        base = captured.document; savedSerial = captured.serial; operation = null; publish({ revision: ack.revision });
      }
      publish({ state: 'ready', error: null, conflict: false });
    })().catch((error: unknown) => {
      publish({ state: 'failed', error: error instanceof Error ? error.message : String(error), conflict: error instanceof Error && error.name === 'ProjectConflictError' }); throw error;
    }).finally(() => { running = null; });
    return running;
  }
  return {
    snapshot: () => current,
    subscribe: (listener: () => void) => { listeners.add(listener); return () => { listeners.delete(listener); }; },
    hydrate, patch, flush,
    async reload() {
      if (running) await running.catch(() => {});
      const previous = current, previousSerial = serial, previousSaved = savedSerial;
      clearTimeout(timer); loading = null; hydrated = false; beforeHydration = {}; serial = savedSerial = 0; unsaved.clear();
      try { const remote = await hydrate(); operation = null; return remote; }
      catch (error) { serial += previousSerial; savedSerial = previousSaved; hydrated = true; publish({ ...previous, document: { ...previous.document, ...beforeHydration }, state: 'failed', error: error instanceof Error ? error.message : String(error) }); throw error; }
    },
  };
}
