import type { Config, JobRequest, JobStatus, PresetCatalog, PresetType, ProgressEvent, SystemPreset, UserPreset } from './types';

export interface Api {
  mock: boolean;
  presets(vendor: string): Promise<PresetCatalog>;
  userPresets(): Promise<UserPreset[]>;
  saveUserPreset(p: UserPreset): Promise<void>;
  deleteUserPreset(type: PresetType, name: string): Promise<void>;
  submit(req: JobRequest): Promise<string>;
  status(id: string): Promise<JobStatus>;
  events(id: string, on: { progress(e: ProgressEvent): void; done(s: JobStatus): void; error(msg: string): void; log?(stream: string, line: string): void }): () => void;
  cancel(id: string): Promise<void>;
  downloadUrl(id: string): string;
  movesUrl(id: string): string;
}

async function json<T>(r: Response): Promise<T> {
  if (!r.ok) throw new Error(`${r.status} ${r.statusText}: ${(await r.text()).slice(0, 300)}`);
  return r.json() as Promise<T>;
}

function normalizeCatalog(raw: any, vendor: string): PresetCatalog {
  const pick = (...keys: string[]) => keys.map((k) => raw?.[k]).find(Array.isArray) ?? [];
  const norm = (type: PresetType) => (p: any): SystemPreset => ({
    ...p,
    type,
    vendor: p.vendor ?? vendor,
    config: p.config ?? p.values ?? {},
  });
  return {
    machines: pick('machines', 'machine', 'printers').map(norm('machine')),
    processes: pick('processes', 'process', 'prints').map(norm('process')),
    filaments: pick('filaments', 'filament').map(norm('filament')),
  };
}

export function parseProgress(data: string): ProgressEvent | null {
  try {
    const j = JSON.parse(data);
    const pct = Number(j.pct ?? j.percent ?? j.progress);
    if (Number.isFinite(pct)) return { stage: String(j.stage ?? j.message ?? ''), pct, message: j.message };
  } catch {
  }
  const m = /progress\s+(\S+)\s+(-?[\d.]+)/.exec(data);
  return m ? { stage: m[1], pct: Number(m[2]) } : null;
}

const realApi: Api = {
  mock: false,
  async presets(vendor) {
    return normalizeCatalog(await json(await fetch(`/api/presets?vendor=${encodeURIComponent(vendor)}`)), vendor);
  },
  async userPresets() {
    const r = await fetch('/api/user-presets');
    if (r.status === 404) return mockApi.userPresets();
    const j = await json<any>(r);
    return Array.isArray(j) ? j : (j.presets ?? []);
  },
  async saveUserPreset(p) {
    const r = await fetch('/api/user-presets', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(p) });
    if (r.status === 404) return mockApi.saveUserPreset(p);
    await json(r);
  },
  async deleteUserPreset(type, name) {
    const r = await fetch(`/api/user-presets/${type}/${encodeURIComponent(name)}`, { method: 'DELETE' });
    if (r.status === 404) return mockApi.deleteUserPreset(type, name);
  },
  async submit(req) {
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
    const j = await json<{ id: string }>(await fetch('/api/jobs', { method: 'POST', body: fd }));
    return j.id;
  },
  async status(id) {
    return json<JobStatus>(await fetch(`/api/jobs/${id}`));
  },
  events(id, on) {
    const es = new EventSource(`/api/jobs/${id}/events`);
    let finished = false;
    const finish = async () => {
      if (finished) return;
      finished = true;
      es.close();
      try {
        const s = await realApi.status(id);
        if (s.state === 'done') on.done(s);
        else on.error(s.error ?? `job ${s.state}`);
      } catch (e) {
        on.error(String(e));
      }
    };
    const onData = (ev: MessageEvent) => {
      const p = parseProgress(ev.data);
      if (p) on.progress(p);
      if (/"state"\s*:\s*"(done|failed|canceled)"/.test(ev.data)) void finish();
    };
    es.onmessage = onData;
    es.addEventListener('progress', onData as EventListener);
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
      finished = true;
      es.close();
    };
  },
  async cancel(id) {
    await fetch(`/api/jobs/${id}`, { method: 'DELETE' });
  },
  downloadUrl: (id) => `/api/jobs/${id}/download`,
  movesUrl: (id) => `/api/jobs/${id}/moves`,
};

const USER_KEY = 'orca.userPresets';
const mockJobs = new Map<string, { req: JobRequest; status: JobStatus; gcode?: string }>();

const STAGES: [string, number][] = [
  ['loading', 2], ['slicing', 10], ['perimeters', 25], ['infill', 40], ['nonplanar', 55],
  ['support', 65], ['skirt', 72], ['gcode', 80], ['processing', 92], ['export', 98],
];

const mockApi: Api = {
  mock: true,
  async presets(vendor) {
    const all = normalizeCatalog((await import('../mock/presets.json')).default, vendor);
    if (vendor === '*') return all;
    const f = (l: SystemPreset[]) => l.filter((p) => p.vendor === vendor);
    return { machines: f(all.machines), processes: f(all.processes), filaments: f(all.filaments) };
  },
  async userPresets() {
    try {
      return JSON.parse(localStorage.getItem(USER_KEY) ?? '[]') as UserPreset[];
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
    let i = 0;
    j.status.state = 'running';
    const timer = setInterval(() => {
      if (i < STAGES.length) {
        const [stage, pct] = STAGES[i++];
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
      on.progress({ stage: 'done', pct: 100 });
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
