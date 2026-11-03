import { submissionHeaders } from '../flow/submission';
import { bootstrapAccessToken, mutation, responseError } from './access';
import { expandCatalog } from './compact-catalog';
import { normalizeUserPreset } from '../profiles/model';
import { catalogWorker, onWorkerFailure, workerId } from './worker';
import type { CatalogIn, CatalogOut } from './catalog.worker';
import type { Config, JobRequest, JobStatus, PresetCatalog, ProgressEvent, UserPreset } from './types';

export interface Api {
  mock: boolean;
  presets(vendor: string, models: string[], update?: (c: PresetCatalog) => void): Promise<PresetCatalog>;
  userPresets(): Promise<UserPreset[]>;
  submit(req: JobRequest, signal?: AbortSignal, onUpload?: (fraction: number) => void): Promise<string>;
  status(id: string): Promise<JobStatus>;
  events(id: string, on: { progress(e: ProgressEvent): void; done(s: JobStatus): void; error(msg: string): void; log?(stream: string, line: string): void }): () => void;
  cancel(id: string): Promise<void>;
  downloadUrl(id: string): string;
  movesUrl(id: string): string;
}

async function json<T>(r: Response): Promise<T> {
  if (!r.ok) throw await responseError(r);
  return r.json() as Promise<T>;
}

export function parseProgress(data: string): ProgressEvent | null {
  try {
    const j = JSON.parse(data);
    const value = j?.pct ?? j?.percent ?? j?.progress;
    const pct = typeof value === 'number' || typeof value === 'string' && value.trim() ? Number(value) : NaN;
    if (j && typeof j === 'object' && Number.isFinite(pct)) return { stage: String(j.stage ?? j.message ?? ''), pct, message: j.message };
  } catch {
  }
  const m = /^\s*progress\s+(\S+)\s+(-?\d+(?:\.\d+)?)\s*$/.exec(data);
  return m && Number.isFinite(Number(m[2])) ? { stage: m[1], pct: Number(m[2]) } : null;
}

const realApi: Api = {
  mock: false,
  presets(vendor, models, update) {
    const worker = catalogWorker();
    const id = workerId();
    return new Promise((resolve, reject) => {
      let answered = false;
      const cleanup = () => { worker.removeEventListener('message', on); unsubscribe(); };
      const fail = (error: Error) => { cleanup(); if (!answered) reject(error); };
      const on = (e: MessageEvent<CatalogOut>) => {
        const m = e.data;
        if (m.id !== id) return;
        if (m.type === 'catalog') {
          const catalog = expandCatalog(m.catalog);
          if (answered) update?.(catalog);
          else resolve(catalog);
          answered = true;
        } else if (m.type === 'done') cleanup();
        else if (m.type === 'error') fail(new Error(m.message));
      };
      const unsubscribe = onWorkerFailure(fail);
      worker.addEventListener('message', on);
      worker.postMessage({ id, vendor, models } satisfies CatalogIn);
    });
  },
  async userPresets() {
    const {readProfileLibrary}=await import('../persist/profile-library');
    const library=await readProfileLibrary();
    const {rememberBase}=await import('../persist/profiles');library.catalog?.forEach(rememberBase);
    return library.profiles.map(normalizeUserPreset);
  },

  async submit(req, signal, onUpload) {
    if (req.backend && toolFeatures.orcanp_v01 !== true) throw new Error('Orca-NP v0.1 is unavailable on this service. Choose Standard or connect the external engine.');
    const fd = new FormData();
    for (const f of req.files) fd.append('files[]', f.blob, f.name);
    if (req.backend) fd.append('backend', req.backend);
    if(req.profile_ids)fd.append('profile_ids',JSON.stringify(req.profile_ids));
    fd.append('vendor', req.vendor);
    fd.append('printer', req.printer);
    fd.append('process', req.process);
    for (const f of req.filaments) fd.append('filament[]', f);
    fd.append('overrides', JSON.stringify(req.overrides));
    if (req.plate_index !== undefined) fd.append('plate_index', String(req.plate_index));
    if (req.plate) fd.append('plate', JSON.stringify(req.plate));
    if (req.project_instances) fd.append('project_instances', JSON.stringify(req.project_instances));
    if (req.events.length) fd.append('events', JSON.stringify(req.events));
    fd.append('output', req.output);
    if (req.priority) fd.append('priority', req.priority);
    if (req.project_settings) fd.append('project_settings', JSON.stringify(req.project_settings));
    if (req.project_edits) fd.append('project_edits', JSON.stringify(req.project_edits));
    if (req.force_planar) fd.append('force_planar', '1');
    if (req.floating) fd.append('floating', '1');
    if (req.allow_mixed_temperatures) fd.append('allow_mixed_temperatures', '1');
    if (req.calib) { fd.append('calib', req.calib); fd.append('calib_params', req.calib_params ?? ''); if (req.calib_head) fd.append('calib_head', req.calib_head); }
    if (req.preview_budget !== undefined) fd.append('budget', String(req.preview_budget));
    const url = req.backend ? '/api/backends/orcanp-v01/jobs' : req.preview_budget !== undefined ? '/api/patch-preview' : '/api/jobs';
    const j = await json<{ id: string }>(await mutation(url, { method: 'POST', headers: submissionHeaders(req), body: fd }, req.preview_budget === undefined, signal, onUpload, req.preview_budget !== undefined));
    return j.id;
  },
  async status(id) {
    return json<JobStatus>(await fetch(`/api/jobs/${id}`));
  },
  events(id, on) {
    const es = new EventSource(`/api/jobs/${id}/events`);
    let finished = false;
    let disposed = false;
    let checking = false;
    let reason: string | undefined;
    const finish = async () => {
      if (finished || checking) return;
      checking = true;
      try {
        const s = await realApi.status(id);
        if (disposed) return;
        if (s.state === 'queued' || s.state === 'running') return;
        finished = true;
        es.close();
        if (s.state === 'done') on.done(s);
        else on.error(s.error ?? `job ${s.state}`);
      } catch (e) {
        if (!disposed) {
          finished = true;
          es.close();
          on.error(reason ?? String(e));
        }
      } finally {
        checking = false;
      }
    };
    const onData = (ev: MessageEvent) => {
      if (finished) return;
      const p = parseProgress(ev.data);
      if (p) on.progress(p);
      if (/"state"\s*:\s*"(done|failed|canceled)"/.test(ev.data)) void finish();
    };
    es.onmessage = onData;
    es.addEventListener('progress', onData as EventListener);
    es.addEventListener('state', onData as EventListener);
    es.addEventListener('log', ((ev: MessageEvent) => {
      try {
        const j = JSON.parse(ev.data);
        on.log?.(String(j.stream ?? ''), String(j.line ?? ''));
      } catch {
      }
    }) as EventListener);
    for (const t of ['done', 'end', 'complete', 'failed', 'error_event'])
      es.addEventListener(t, ((ev: MessageEvent) => {
        try {
          reason = JSON.parse(ev.data).error || reason;
        } catch {
        }
        void finish();
      }) as EventListener);
    es.onerror = () => void finish();
    return () => {
      disposed = true;
      finished = true;
      es.close();
    };
  },
  async cancel(id) {
    await mutation(`/api/jobs/${id}`, { method: 'DELETE' });
  },
  downloadUrl: (id) => `/api/jobs/${id}/download`,
  movesUrl: (id) => `/api/jobs/${id}/moves`,
};


export let toolFeatures: Record<string, unknown> = {};
export interface SurfaceArtifactManifest {
  validated: boolean;
  preview_ready?: boolean;
  print_qualified?: boolean;
  backend?: string;
  source_sha?: string;
  binary_sha256: string;
  resources_sha256: string;
  features: { curved_tops?: boolean; nonplanar_wave?: boolean };
  wave_modes?: string[];
}
export let toolArtifactManifest: SurfaceArtifactManifest | undefined;
const validatedSurface = (feature: 'curved_tops' | 'nonplanar_wave') => toolFeatures[feature] === true
  && (toolArtifactManifest?.preview_ready ?? toolArtifactManifest?.validated) === true
  && /^[a-f0-9]{64}$/i.test(toolArtifactManifest?.binary_sha256 ?? '')
  && /^[a-f0-9]{64}$/i.test(toolArtifactManifest?.resources_sha256 ?? '')
  && toolArtifactManifest?.features?.[feature] === true;
export const canCurvedTops = () => validatedSurface('curved_tops');
export const canSurfaceWaves = (mode = 'whole') => validatedSurface('nonplanar_wave') && toolArtifactManifest?.wave_modes?.includes(mode) === true;

export function surfaceConfigBlocker(config: Config): string | undefined {
  if (String(config.nonplanar_mode) === 'curved_tops' && !canCurvedTops()) return 'Curved layers need a newer slicing engine';
  const wave = String(config.nonplanar_wave ?? 'off');
  if (wave !== 'off' && !canSurfaceWaves(wave)) return 'Rippled top needs a newer slicing engine';
}

function guardedApi(api: Api): Api {
  return { ...api, async submit(request, signal, onUpload) {
    if (request.output !== '3mf' && !request.force_planar) {
      const objects = [...request.plate?.objects ?? [], ...request.project_edits?.filter((e) => e.op === 'add_object') ?? []];
      const configs: Config[] = [request.overrides, request.project_settings ?? {},
        ...request.plate?.plates?.map((p) => p.settings) ?? [],
        ...objects.flatMap((o) => [o.settings ?? {}, ...o.volumes?.map((v) => v.settings ?? {}) ?? [], ...o.ranges?.map((r) => r.settings) ?? []]),
        ...request.project_edits?.flatMap((e) => 'settings' in e ? [e.settings ?? {}] : []) ?? []];
      for (const config of configs) {
        const reason = surfaceConfigBlocker(config);
        if (reason) throw new Error(reason);
      }
    }
    return api.submit(request, signal, onUpload);
  } };
}
export const canLoadStep = () => toolFeatures.step === true;
export const canEditProject = () => toolFeatures.edit === true;
export const canForcePlanar = () => toolFeatures.force_planar === true;
export const canCalibrate = () => toolFeatures.calib === true;
export const canOneShot = () => toolFeatures.oneshot === true;

export async function connect(): Promise<Api> {
  bootstrapAccessToken();
  toolFeatures = {};
  toolArtifactManifest = undefined;
  try {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), 1500);
    const r = await fetch('/api/healthz', { signal: ctl.signal, method: 'GET' });
    clearTimeout(t);
    const type = r.headers.get('content-type') ?? '';
    if (r.ok && type.includes('json')) {
      const health = await r.json() as { tool_features?: Record<string, unknown>; artifact_manifest?: SurfaceArtifactManifest };
      toolFeatures = health.tool_features ?? {};
      toolArtifactManifest = health.artifact_manifest;
      return guardedApi(realApi);
    }
  } catch {
  }
  throw new Error('The slicing service is unavailable. Reconnect and retry.');
}
