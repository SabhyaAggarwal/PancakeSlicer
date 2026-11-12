import type { ServicePresets } from '../api/catalog';
import { randomId } from '../random-id';
type Row = Record<string, any>;
interface Seed { library: Row; preferences: Row; session: Row; health: Row; options: Row; tooltips: Row;
  printers: Row; filaments: Row; presets: ServicePresets; projects: Row[]; assets: Record<string, string>; covers: Record<string, string> }
interface DemoJob { id: string; fixture: string; created: number; cancelled?: boolean }
interface DemoState { library: Row; preferences: Row; session: Row; projects: Record<string, Row>; drafts: Record<string, Row>;
  assets: Record<string, string>; jobs: Record<string, DemoJob>; versions: Record<string, Row[]>; folders: Row[]; folderRevision: number }
let hooks: { notice?(message: string): void; resolve?(input: import('./resolve').DemoResolveInput): Promise<unknown> } = {};
export const configureDemo = (value: typeof hooks) => { hooks = value; };
const STORAGE = 'pancake.demo.v1';
const files = import.meta.glob('./fixtures/*', { eager: true, query: '?url&no-inline', import: 'default' }) as Record<string, string>;
const nativeFetch = globalThis.fetch.bind(globalThis), mainThread = typeof window !== 'undefined';
let installed = false, seedRead: Promise<Seed> | undefined, stateRead: Promise<DemoState> | undefined;
const json = (value: unknown, status = 200) => new Response(JSON.stringify(value), { status, headers: { 'Content-Type': 'application/json' } });
const missing = () => json({ error: 'This item isn\'t available in the live demo.' }, 404);
const assetUrl = (name: string) => { const url = files[`./fixtures/${name}`]; if (!url) throw new Error(`Oops! Demo sample not found: ${name}`); return url; };
async function compressed(name: string) {
  const response = await nativeFetch(assetUrl(name));
  if (!response.ok || !response.body) throw new Error(`Oops! Couldn't load demo sample: ${name}`);
  return new Response(response.body.pipeThrough(new DecompressionStream('gzip')));
}
const seed = () => seedRead ??= compressed('seed.json.gz').then(r => r.json() as Promise<Seed>);
function initial(s: Seed): DemoState { return structuredClone({ library: s.library, preferences: s.preferences, session: s.session,
  projects: Object.fromEntries(s.projects.map(p => [p.meta.id, p])), drafts: {}, assets: {}, jobs: {}, versions: {}, folders: [], folderRevision: 0 }); }
async function state(): Promise<DemoState> {
  return stateRead ??= seed().then(s => {
    if (mainThread) { try { const saved = sessionStorage.getItem(STORAGE); if (saved) return JSON.parse(saved) as DemoState; } catch {  } }
    return initial(s);
  });
}
function save(s: DemoState) {
  if (!mainThread) return;
  try { sessionStorage.setItem(STORAGE, JSON.stringify(s)); }
  catch { hooks.notice?.('Heads up: browser storage for this demo is full. Keep this tab open so you don\'t lose your edits.'); }
}
export async function resetDemo() {
  const next = initial(await seed());
  next.preferences = { revision: 0, document: {} };
  next.session = { revision: 0, document: { demoOnboarding: true } };
  stateRead = Promise.resolve(next); save(next);
}
async function body(request: Request): Promise<Row> {
  if (!request.body) return {};
  const text = request.headers.get('content-encoding') === 'gzip'
    ? await new Response(request.body.pipeThrough(new DecompressionStream('gzip'))).text() : await request.text();
  return text ? JSON.parse(text) : {};
}
function version(s: DemoState, id: string) {
  const p = s.projects[id]; if (!p) return null;
  const list = s.versions[id] ??= []; let v = list.at(-1);
  if (!v || v.revision !== p.meta.revision) {
    v = { id: list.length + 1, revision: p.meta.revision, at: p.meta.savedAt, until: p.meta.savedAt, name: p.meta.name,
      objects: p.meta.objects, record: String(p.meta.revision), change: [{ label: list.length ? 'Edited' : 'Created' }], project: structuredClone(p) }; list.push(v);
  }
  return v;
}
const fixtureOf = (id: string) => /^demo-(benchy-(?:flat|curved)-\d{3})-/.exec(id)?.[1] ?? 'benchy-flat-020';
const statuses = new Map<string, Promise<Row>>();
let gcode: { fixture: string; lines: Promise<string[]> } | undefined;
function gcodeLines(fixture: string) {
  if (gcode?.fixture !== fixture) gcode = { fixture, lines: compressed(`${fixture}.gcode.gz`).then(r => r.text()).then(text => text.split('\n')) };
  return gcode.lines;
}
function fixtureStatus(name: string) {
  let read = statuses.get(name); if (!read) { read = compressed(`${name}.status.json.gz`).then(r => r.json()); statuses.set(name, read); } return read;
}
function phase(j: DemoJob) {
  const elapsed = Date.now() - j.created;
  return j.cancelled ? { state: 'cancelled', stage: 'cancelled', progress: 0 }
    : elapsed < 350 ? { state: 'queued', stage: 'queued', progress: 0 }
    : elapsed < 2300 ? { state: 'slicing', stage: 'slicing', progress: Math.min(90, Math.round((elapsed - 350) / 22)) }
    : elapsed < 2900 ? { state: 'storing', stage: 'storing', progress: 95 }
    : { state: 'done', stage: 'done', progress: 100 };
}
async function snapshot(j: DemoJob) {
  const p = phase(j), raw = p.state === 'done' ? await fixtureStatus(j.fixture) : {};
  return { ...raw, ...p, id: j.id, created: new Date(j.created).toISOString(), download_url: `/api/jobs/${j.id}/download`,
    events_url: `/api/jobs/${j.id}/events`, queue_position: p.state === 'queued' ? 1 : 0 };
}
function events(j: DemoJob, signal: AbortSignal, after: number) {
  const timers: ReturnType<typeof setTimeout>[] = []; let closed = false;
  const stream = new ReadableStream<Uint8Array>({
    start(controller) {
      const close = () => { if (closed) return; closed = true; timers.forEach(clearTimeout); controller.close(); };
      const emit = async (seq: number) => {
        if (closed) return;
        const p = phase(j), done = p.state === 'done' || p.state === 'cancelled';
        const data = done ? await snapshot(j) : { ...p, id: j.id, pct: p.progress };
        if (closed) return;
        if (seq > after) controller.enqueue(new TextEncoder().encode(`id: ${seq}\nevent: ${done ? 'done' : 'state'}\ndata: ${JSON.stringify(data)}\n\n`));
        if (done) close();
      };
      [0, 400, 1000, 1700, 2350, 2950].forEach((at, seq) => timers.push(setTimeout(() => void emit(seq), Math.max(0, j.created + at - Date.now()))));
      signal.addEventListener('abort', close, { once: true });
    }, cancel() { closed = true; timers.forEach(clearTimeout); },
  });
  return new Response(stream, { headers: { 'Content-Type': 'text/event-stream' } });
}
async function base64(blob: Blob) {
  const bytes = new Uint8Array(await blob.arrayBuffer()), pieces: string[] = [];
  for (let at = 0; at < bytes.length; at += 16384) pieces.push(String.fromCharCode(...bytes.subarray(at, at + 16384)));
  return btoa(pieces.join(''));
}
const from64 = (value: string) => new Response(Uint8Array.from(atob(value), c => c.charCodeAt(0)));
async function handle(request: Request): Promise<Response> {
  const url = new URL(request.url), path = decodeURIComponent(url.pathname).slice(5), method = request.method, data = await seed();
  if (path === 'healthz') return json(data.health);
  if (path === 'update') return json({ release: null });
  if (path === 'options') return json({ ...data.options, devReset: false });
  if (path === 'options/tooltips') return json(data.tooltips);
  if (path === 'printer-catalog') return json(data.printers);
  if (path.startsWith('printer-catalog/covers/')) return data.covers[path] ? from64(data.covers[path]) : missing();
  if (path === 'filament-catalog') return json(data.filaments);
  if (path === 'presets') {
    const names = url.searchParams.getAll('name'), models = url.searchParams.getAll('model'), raw = data.presets;
    const machines = raw.machines.filter(p => !names.length && !models.length || names.includes(p.name) || models.includes(String(p.model)));
    const selected = new Set(machines.map(p => p.name));
    const keep = (p: typeof raw.machines[number]) => !names.length && !models.length || names.includes(p.name) || !names.length && (p.compatible_printers ?? []).some(n => selected.has(n));
    return json({ ...raw, machines, processes: raw.processes.filter(keep), filaments: raw.filaments.filter(keep) });
  }
  if (path === 'user-heads' || path === 'printer-sends') return json([]);
  if (path === 'emboss-fonts') return json({ fonts: [] });
  if (path === 'printer-connections') return json({ enabled: true, connections: [] });
  if (path.startsWith('printer-connections/')) return json({ error: 'Not available in demo mode' }, 400);
  if (path.startsWith('project-leases/')) return json({ granted: true, heartbeatMs: 60000 });
  if (path.startsWith('handy-models/')) {
    const model = path.slice('handy-models/'.length).toLowerCase();
    const sample = data.projects.find(p => /benchy/.test(model) ? /benchy/i.test(p.meta.name) : /cube/i.test(p.meta.name));
    const key = sample?.doc.objects[0]?.source;
    return key && data.assets[key] ? from64(data.assets[key]) : missing();
  }
  if (path === 'about') return json({ name: 'PancakeSlicer Demo', version: 'Client-side demo', source_url: 'https://github.com/ZimengXiong/nonplanar-orcaslicer/tree/np/wa-orcaparity-1', source_configured: true, license: 'AGPL-3.0', revision: data.health.server_revision });
  const s = await state();
  if (path === 'resolve') {
    try { return json(await hooks.resolve?.(await body(request) as import('./resolve').DemoResolveInput)); }
    catch (error) { return json({ error: error instanceof Error ? error.message : String(error) }, 400); }
  }
  if (path === 'library') {
    if (method === 'GET') return json(s.library);
    const input = await body(request); s.library = { ...input.document, revision: input.expectedRevision + 1 }; save(s);
    return json({ revision: s.library.revision, operationId: input.operationId });
  }
  if (path === 'session' || path === 'preferences') {
    if (method === 'GET') return json(s[path]);
    const input = await body(request); s[path] = { revision: input.expectedRevision + 1, document: input.document }; save(s);
    return json({ revision: s[path].revision, operationId: input.operationId });
  }
  if (path.startsWith('assets/') || path.startsWith('blobs/')) {
    const key = path.slice(path.indexOf('/') + 1); if (key === 'missing') return json({ missing: [] });
    if (method === 'PUT') { let blob = await request.blob(); if (request.headers.get('content-encoding') === 'gzip') blob = await new Response(blob.stream().pipeThrough(new DecompressionStream('gzip'))).blob(); s.assets[key] = await base64(blob); save(s); return json({}); }
    const bytes = s.assets[key] ?? data.assets[key]; return bytes ? method === 'HEAD' ? new Response(null) : from64(bytes) : missing();
  }
  if (path === 'drafts') return json(Object.values(s.drafts));
  if (path.startsWith('drafts/')) {
    const id = path.slice(7);
    if (method === 'DELETE') { delete s.drafts[id]; save(s); return json({}); }
    if (method === 'PUT') { const input = await body(request); s.drafts[id] = { ...input, id }; save(s); return json({ sequence: input.sequence }); }
    return s.drafts[id] ? json(s.drafts[id]) : missing();
  }
  if (path === 'projects/references') return json({ complete: true, documents: Object.values(s.projects).map(p => ({ id: p.meta.id, name: p.meta.name, doc: p.doc, trashed: !!p.archived })) });
  if (path === 'projects') return json(Object.values(s.projects).filter(p => !!p.archived === (url.searchParams.get('archived') === '1')).map(p => p.meta).sort((a, b) => b.savedAt - a.savedAt));
  if (path.startsWith('projects/')) {
    const [, id, action] = path.split('/'), p = s.projects[id];
    if (method === 'GET') return p ? json(p) : missing();
    const input = await body(request);
    if (method === 'PUT') { const revision = input.expectedRevision + 1; s.projects[id] = { ...input.record, meta: { ...input.record.meta, id, revision } }; version(s, id); save(s); return json({ revision, operationId: input.operationId }); }
    if (!p) return missing();
    if (action === 'duplicate') { const copy = structuredClone(p); copy.meta = { ...copy.meta, id: input.newId, name: input.name, revision: 1, savedAt: Date.now() }; s.projects[input.newId] = copy; version(s, input.newId); save(s); return json(copy.meta); }
    if (action === 'archive' || action === 'restore') p.archived = action === 'archive';
    if (action === 'rename') p.meta.name = input.name;
    p.meta.revision = input.expectedRevision + 1; version(s, id); save(s); return json({ revision: p.meta.revision, operationId: input.operationId });
  }
  if (path.startsWith('versions/')) {
    const [, id, v, action] = path.split('/'), latest = version(s, id), list = s.versions[id] ?? [];
    if (v === 'seal') { if (latest) latest.sealed = true; save(s); return json(latest); }
    if (!v) return json({ revision: s.projects[id]?.meta.revision ?? 0, versions: [...list].reverse().map(({ project: _, ...row }) => row) });
    const found = list.find(row => row.id === Number(v)); if (!found) return missing();
    if (action === 'summary') return json(found.project);
    if (action === 'slice') {
      const input = await body(request), job = s.jobs[input.jobId];
      if (!job) return missing();
      const { record } = await fixtureStatus(job.fixture), stats = record.statistics;
      found.jobId = input.jobId;
      found.slice = { at: Date.now(), timeS: record.totals.estimated_time_s, grams: stats.total_weight_g,
        mm: stats.total_used_filament_mm, layers: job.fixture.endsWith('012') ? 399 : 240 };
      save(s); return json(found);
    }
    if (action === 'open-slice' && found.jobId) return json(await snapshot(s.jobs[found.jobId]));
    return json(found.project);
  }
  if (path === 'folders') {
    if (method === 'GET') return json({ revision: s.folderRevision, folders: s.folders });
    const input = await body(request), folder = { id: input.id, name: input.name, parentId: input.parentId ?? null, revision: 1, createdAt: Date.now(), updatedAt: Date.now() }; s.folders.push(folder); s.folderRevision++; save(s); return json({ revision: s.folderRevision, folder });
  }
  if (path.startsWith('folders/')) {
    const [, id, action] = path.split('/'), input = await body(request);
    if (id === 'move') { for (const key of input.ids ?? input.projectIds ?? []) if (s.projects[key]) s.projects[key].meta.folderId = input.folderId; }
    else if (action === 'delete') {
      const parent = s.folders.find(f => f.id === id)?.parentId ?? null;
      s.folders = s.folders.filter(f => f.id !== id).map(f => f.parentId === id ? { ...f, parentId: parent } : f);
      for (const p of Object.values(s.projects)) if (p.meta.folderId === id) p.meta.folderId = null;
    }
    else { const folder = s.folders.find(f => f.id === id); if (folder) Object.assign(folder, input); }
    s.folderRevision++; save(s); return json({ revision: s.folderRevision });
  }
  if (path === 'jobs' || path === 'patch-preview') {
    const form = await request.formData();
    if (form.get('output') === '3mf') return json({ error: 'That model tool requires the full slicing service and isn\'t available in the browser demo. You can still import STL or OBJ files!' }, 400);
    const input = JSON.parse(String(form.get('library') ?? '{}'));
    const settings = { ...s.library.profiles.find((p: Row) => p.id === input.print)?.intent, ...input.project };
    const curved = !form.has('force_planar') && settings.nonplanar_mode === 'nonplanar_top', height = Number(input.layerHeight || settings.layer_height || 0.2);
    const fixture = curved ? 'benchy-curved-020' : height < 0.16 ? 'benchy-flat-012' : 'benchy-flat-020', id = `demo-${fixture}-${randomId()}`;
    s.jobs[id] = { id, fixture, created: Date.now() }; save(s); return json({ id });
  }
  if (path.startsWith('jobs/')) {
    const [, id, action] = path.split('/'), j = s.jobs[id] ?? { id, fixture: fixtureOf(id), created: 0 };
    if (method === 'DELETE' || action === 'cancel') { j.cancelled = true; save(s); return json(await snapshot(j)); }
    if (action === 'retry') { j.created = Date.now(); j.cancelled = false; save(s); return json(await snapshot(j)); }
    if (action === 'events') return events(j, request.signal, Number(request.headers.get('Last-Event-ID') ?? -1));
    if (action === 'moves') return nativeFetch(assetUrl(`${j.fixture}.moves.gz`));
    if (action === 'download') return compressed(`${j.fixture}.gcode.gz`);
    if (action === 'gcode-lines') { const lines = await gcodeLines(j.fixture), at = Number(url.searchParams.get('block') ?? 0) * 1024; return json({ total: lines.length, lines: lines.slice(at, at + 1024) }); }
    if (action === 'surface-grid') return compressed(`${j.fixture}.surface.json.gz`);
    if (action === 'send') return json({ error: 'Not available in demo mode' }, 400);
    if (action === 'transitions') return json([]);
    return json(await snapshot(j));
  }
  return missing();
}
export function installDemoTransport() {
  if (installed) return; installed = true;
  globalThis.fetch = async (input, init) => {
    const raw = input instanceof Request ? input.url : String(input), url = new URL(raw, globalThis.location.href);
    if (!url.pathname.startsWith('/api/')) return nativeFetch(input, init);
    const request = new Request(url, input instanceof Request ? input : init);
    if (request.signal.aborted) throw new DOMException('Request aborted', 'AbortError');
    return handle(request);
  };
  if (mainThread) {
    const beacon = navigator.sendBeacon.bind(navigator);
    navigator.sendBeacon = (url, data) => String(url).startsWith('/api/') || beacon(url, data);
    if (!location.hash) void state().then(s => {
      const id = s.session.document.demoProject ?? Object.keys(s.projects)[0];
      if (id && !s.session.document.demoOnboarding && s.library.profiles.some((p: Row) => p.type === 'printer')) history.replaceState(null, '', `${location.pathname}#/p/${id}`);
    });
  }
}
