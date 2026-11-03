import url from 'virtual:mock-presets';
import { normalizeConfig } from '../config-values';
import { catalogWorker, onWorkerFailure, workerId } from './worker';
import type { CatalogOut } from './catalog.worker';
import { expandCatalog } from './compact-catalog';
import { demoPresets } from './catalog';
import type { PresetCatalog, SystemPreset } from './types';

let pending: Promise<PresetCatalog> | undefined;
function load(): Promise<PresetCatalog> {
  return new Promise((resolve, reject) => {
    const worker = catalogWorker();
    const id = workerId();
    const cleanup = () => {
      worker.removeEventListener('message', on);
      unsubscribe();
    };
    const fail = (error: Error) => { cleanup(); reject(error); };
    const on = (e: MessageEvent<CatalogOut>) => {
      if (e.data.id !== id) return;
      if (e.data.type === 'catalog') {
        cleanup();
        resolve(expandCatalog(e.data.catalog, normalizeConfig));
      } else if (e.data.type === 'error') {
        cleanup();
        reject(new Error(e.data.message));
      }
    };
    const unsubscribe = onWorkerFailure(fail);
    worker.addEventListener('message', on);
    worker.postMessage({ id, type: 'mock', url: new URL(url, location.href).href });
  });
}

export async function mockPresets(vendor: string, models: string[]): Promise<PresetCatalog> {
  const all = await (pending ??= load().catch((error) => { pending = undefined; throw error; }));
  const filter = (list: SystemPreset[]) => list.filter((p) => vendor === '*' || p.vendor === vendor);
  return demoPresets({ machines: filter(all.machines), processes: filter(all.processes), filaments: filter(all.filaments) }, models);
}
