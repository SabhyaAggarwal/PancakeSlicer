import { bootstrapAccessToken, mutation, responseError } from './access';
import { normalizeConfig } from '../config-values';
import { demoPresets, normalizeCatalog } from './catalog';
import { catalogWorker, workerId } from './worker';
import type { CatalogIn, CatalogOut } from './catalog.worker';
import type { Config, JobRequest, JobStatus, PresetCatalog, PresetType, ProgressEvent, SystemPreset, UserPreset } from './types';

export interface Api {
  mock: boolean;
  presets(vendor: string, models: string[], update?: (c: PresetCatalog) => void): Promise<PresetCatalog>;
  userPresets(): Promise<UserPreset[]>;
  saveUserPreset(p: UserPreset): Promise<void>;
  deleteUserPreset(type: PresetType, name: string): Promise<void>;
  submit(req: JobRequest, signal?: AbortSignal): Promise<string>;
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

const normalizeUserPreset = (preset: UserPreset): UserPreset => ({ ...preset, overrides: normalizeConfig(preset.overrides) });

const realApi: Api = {
  mock: false,
  presets(vendor, models, update) {
    const worker = catalogWorker();
    const id = workerId();
    return new Promise((resolve, reject) => {
      let answered = false;
      const on = (e: MessageEvent<CatalogOut>) => {
        const m = e.data;
        if (m.id !== id) return;
        if (m.type === 'catalog') {
          if (answered) update?.(m.catalog);
          else resolve(m.catalog);
          answered = true;
        } else if (m.type === 'done' || m.type === 'error') {
          worker.removeEventListener('message', on);
          if (m.type === 'error' && !answered) reject(new Error(m.message));
        }
      };
      worker.addEventListener('message', on);
      worker.postMessage({ id, vendor, models } satisfies CatalogIn);
    });
  },
  async userPresets() {
    const r = await fetch('/api/user-presets');
    if (r.status === 404) return mockApi.userPresets();
    const j = await json<any>(r);
    return (Array.isArray(j) ? j : (j.presets ?? [])).map(normalizeUserPreset);
  },
  async saveUserPreset(p) {
    const r = await mutation('/api/user-presets', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(p) });
    if (r.status === 404) return mockApi.saveUserPreset(p);
    await json(r);
  },
  async deleteUserPreset(type, name) {
    const r = await mutation(`/api/user-presets/${type}/${encodeURIComponent(name)}`, { method: 'DELETE' });
    if (r.status === 404) return mockApi.deleteUserPreset(type, name);
  },
  async submit(req, signal) {
    const fd = new FormData();
    for (const f of req.files) fd.append('files[]', f.blob, f.name);
    fd.append('vendor', req.vendor);
    fd.append('printer', req.printer);
    fd.append('process', req.process);
    for (const f of req.filaments) fd.append('filament[]', f);
    fd.append('overrides', JSON.stringify(req.overrides));
    if (req.plate) fd.append('plate', JSON.stringify(req.plate));
    if (req.events.length) fd.append('events', JSON.stringify(req.events));
    fd.append('output', req.output);
    const j = await json<{ id: string }>(await mutation('/api/jobs', { method: 'POST', body: fd }, true, signal));
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
          on.error(String(e));
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
    for (const t of ['done', 'end', 'complete', 'failed', 'error_event']) es.addEventListener(t, () => void finish());
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

const USER_KEY = 'orca.userPresets';
const mockJobs = new Map<string, { req: JobRequest; status: JobStatus; gcode?: string }>();

const STAGES: [string, number][] = [
  ['load', 0], ['load', 100], ['slice', 0], ['slice', 15], ['slice', 25], ['slice', 35],
  ['slice', 40], ['slice', 50], ['slice', 70], ['slice', 75], ['slice', 100],
  ['export', 0], ['export', 100], ['verify', 0], ['verify', 100],
  ['package', 0], ['package', 100], ['json', 0], ['json', 100],
];

const mockApi: Api = {
  mock: true,
  async presets(vendor, models) {
    const [{ gunzipSync, strFromU8 }, { default: url }] = await Promise.all([
      import('fflate'), import('../mock/presets.json.gz?url'),
    ]);
    const response = await fetch(url);
    if (!response.ok) throw new Error('Mock presets failed to load');
    const all = normalizeCatalog(JSON.parse(strFromU8(gunzipSync(new Uint8Array(await response.arrayBuffer())))), vendor);
    const f = (l: SystemPreset[]) => l.filter((p) => vendor === '*' || p.vendor === vendor);
    return demoPresets({ machines: f(all.machines), processes: f(all.processes), filaments: f(all.filaments) }, models);
  },
  async userPresets() {
    try {
      return (JSON.parse(localStorage.getItem(USER_KEY) ?? '[]') as UserPreset[]).map(normalizeUserPreset);
    } catch {
      return [];
    }
  },
  async saveUserPreset(p) {
    const list = (await mockApi.userPresets()).filter((u) => !(u.type === p.type && u.name === p.name));
    localStorage.setItem(USER_KEY, JSON.stringify([...list, p]));
  },
  async deleteUserPreset(type, name) {
    const list = (await mockApi.userPresets()).filter((u) => !(u.type === type && u.name === name));
    localStorage.setItem(USER_KEY, JSON.stringify(list));
  },
  async submit(req) {
    const id = `mock-${Date.now().toString(36)}`;
    mockJobs.set(id, { req, status: { id, state: 'queued', progress: 0, output: req.output } });
    return id;
  },
  async status(id) {
    const j = mockJobs.get(id);
    if (!j) throw new Error('unknown job');
    return j.status;
  },
  events(id, on) {
    const j = mockJobs.get(id)!;
    const stages = STAGES.filter(([stage]) => stage !== 'package' || j.req.output === 'gcode.3mf');
    let i = 0;
    j.status.state = 'running';
    const timer = setInterval(() => {
      if (i < stages.length) {
        const [stage, pct] = stages[i++];
        j.status.stage = stage;
        j.status.progress = pct;
        on.progress({ stage, pct });
        return;
      }
      clearInterval(timer);
      const cfg: Config = j.req.overrides;
      j.gcode = [
        '; generated by OrcaSlicer Non-planar web UI (mock mode, no server)',
        `; printer_settings_id = ${j.req.printer}`,
        `; print_settings_id = ${j.req.process}`,
        `; filament_settings_id = ${j.req.filaments.join(';')}`,
        `; plate = ${JSON.stringify(j.req.plate ?? null)}`,
        `; files = ${JSON.stringify(j.req.files.map((f) => f.name))}`,
        `; overrides = ${JSON.stringify(cfg)}`,
        `; events = ${JSON.stringify(j.req.events)}`,
        'G28 ; home',
        'M84',
        '',
      ].join('\n');
      const base = j.req.plate?.objects[0]?.name ?? j.req.files[0]?.name.replace(/\.3mf$/i, '') ?? 'plate';
      Object.assign(j.status, { state: 'done', progress: 100, stage: 'done', filename: `${base}.${j.req.output}` });
      fetch('/__moves/record')
        .then((r) => (r.ok && (r.headers.get('content-type') ?? '').includes('json') ? r.json() : undefined))
        .catch(() => undefined)
        .then((record) => on.done({ ...j.status, record: record ?? j.status.record }));
    }, 280);
    return () => clearInterval(timer);
  },
  async cancel() {
  },
  downloadUrl(id) {
    const j = mockJobs.get(id);
    return URL.createObjectURL(new Blob([j?.gcode ?? ''], { type: 'text/plain' }));
  },
  movesUrl: () => '/__moves/sample',
};

export async function connect(): Promise<Api> {
  bootstrapAccessToken();
  if (new URLSearchParams(location.search).has('mock')) return mockApi;
  try {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), 1500);
    const r = await fetch('/api/healthz', { signal: ctl.signal, method: 'GET' });
    clearTimeout(t);
    const type = r.headers.get('content-type') ?? '';
    if (r.ok && type.includes('json')) return realApi;
  } catch {
  }
  return mockApi;
}
