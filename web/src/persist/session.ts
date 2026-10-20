import type * as THREE from 'three';
import { loadMesh, loadProject, placeholderGeometry, volumeGeometry } from '../plater/geometry';
import type { ProjectPlate } from '../plater/threemf';
import { documentGeneration, newDocument, onLeaveDocument, snapshot, subscribeHistory, unsaved, type Doc } from '../state/history';
import { getState, reserveIds, setState, subscribeStore, toast, type PlateObject, type State, type Volume } from '../state/store';

type StoredVolume = Omit<Volume, 'geometry'> & { geometry: { position: Float32Array; normal?: Float32Array; index: Uint32Array | Uint16Array | null } };
type StoredObject = Omit<PlateObject, 'geometry' | 'source' | 'volumes'> & { source: string; volumes?: StoredVolume[] };
type StoredDoc = Omit<Doc, 'objects' | 'projectPlates' | 'project'> & {
  objects: StoredObject[];
  projectPlates: (ProjectPlate & { objects: StoredObject[] })[];
  project: string | null;
};
export interface Session {
  id: string;
  name: string;
  savedAt: number;
  objects: number;
  thumbnail: string | null;
  unsaved: boolean;
  blobs: string[];
  doc: StoredDoc;
}

export const MAX_RECENT = 10;
const DELAY_MS = 1000;

let db: IDBDatabase | null = null;
let sessionId = newKey();
let generation = documentGeneration();
let written: Doc | null = null;
let timer = 0;
let queue: Promise<void> = Promise.resolve();
let warned = false;
const blobKeys = new WeakMap<Blob, string>();
const storedBlobs = new Set<string>();
export let offer: Session | null = null;

function newKey() {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}
const keyOf = (b: Blob) => blobKeys.get(b) ?? (blobKeys.set(b, newKey()), blobKeys.get(b)!);

const request = <T>(r: IDBRequest<T>) => new Promise<T>((resolve, reject) => {
  r.onsuccess = () => resolve(r.result);
  r.onerror = () => reject(r.error);
});
const complete = (tx: IDBTransaction) => new Promise<void>((resolve, reject) => {
  tx.oncomplete = () => resolve();
  tx.onerror = tx.onabort = () => reject(tx.error);
});

function openDb(): Promise<IDBDatabase> {
  const r = indexedDB.open('orca-web', 1);
  r.onupgradeneeded = () => {
    r.result.createObjectStore('sessions', { keyPath: 'id' });
    r.result.createObjectStore('blobs');
  };
  return request(r);
}

const plateCount = (d: Pick<State, 'objects' | 'projectPlates' | 'plateIndex'>) =>
  d.objects.length + d.projectPlates.filter((p) => p.index !== d.plateIndex).reduce((n, p) => n + p.objects.length, 0);
const sameDoc = (a: Doc | null, b: Doc) => !!a && (Object.keys(b) as (keyof Doc)[]).every((k) => k === 'selected' || a[k] === b[k]);

function serialize(doc: Doc): { stored: StoredDoc; blobs: Map<string, Blob> } {
  const blobs = new Map<string, Blob>();
  const keep = (b: Blob) => { const k = keyOf(b); blobs.set(k, b); return k; };
  const volume = ({ geometry: g, ...rest }: Volume): StoredVolume => ({ ...rest, geometry: {
    position: g.attributes.position.array as Float32Array, normal: g.attributes.normal?.array as Float32Array | undefined,
    index: (g.index?.array as Uint32Array | undefined) ?? null } });
  const object = ({ geometry: _, source, volumes, ...rest }: PlateObject): StoredObject =>
    ({ ...rest, source: keep(source), ...(volumes ? { volumes: volumes.map(volume) } : {}) });
  return {
    stored: {
      ...doc,
      objects: doc.objects.map(object),
      projectPlates: doc.projectPlates.map((p) => ({ ...p, objects: p.objects.map(object) })),
      project: doc.project && keep(doc.project),
    },
    blobs,
  };
}

function thumbnail(): string | null {
  const p = (window as unknown as { __plater?: { renderer: THREE.WebGLRenderer; scene: THREE.Scene; camera: THREE.Camera } }).__plater;
  const c = p?.renderer.domElement;
  if (!p || !c?.clientWidth || getState().tab !== 'prepare') return null;
  p.renderer.render(p.scene, p.camera);
  const t = document.createElement('canvas');
  t.width = 192;
  t.height = Math.round((192 * c.height) / c.width);
  t.getContext('2d')!.drawImage(c, 0, 0, t.width, t.height);
  return t.toDataURL('image/jpeg', 0.75);
}

export async function sessions(): Promise<Session[]> {
  if (!db) return [];
  const all = await request(db.transaction('sessions').objectStore('sessions').getAll() as IDBRequest<Session[]>);
  return all.sort((a, b) => b.savedAt - a.savedAt);
}

async function prune(keep = sessionId, limit = MAX_RECENT) {
  const all = await sessions();
  const drop = all.filter((s, i) => i >= limit && s.id !== keep);
  const live = new Set(all.filter((s) => !drop.includes(s)).flatMap((s) => s.blobs));
  const blobs = await request(db!.transaction('blobs').objectStore('blobs').getAllKeys());
  const tx = db!.transaction(['sessions', 'blobs'], 'readwrite');
  for (const s of drop) tx.objectStore('sessions').delete(s.id);
  for (const k of blobs as string[]) if (!live.has(k)) { tx.objectStore('blobs').delete(k); storedBlobs.delete(k); }
  await complete(tx);
}

const mb = (n: number) => `${Math.ceil(n / 2 ** 20)} MB`;

async function makeRoom(bytes: number) {
  const free = async () => {
    const e = await navigator.storage?.estimate?.();
    return e?.quota ? e.quota - (e.usage ?? 0) : Infinity;
  };
  for (let limit = MAX_RECENT - 1; (await free()) * 0.9 < bytes; limit--) {
    if (limit < 1) throw new Error(`Autosave is paused: this plate needs ${mb(bytes)} of browser storage and only ${mb(await free())} is free. Save the project (Ctrl+S) to keep it.`);
    await prune(sessionId, limit);
  }
}

async function write(doc: Doc, id: string, state: State) {
  if (!db || sameDoc(written, doc) && id === sessionId) return;
  const count = plateCount(doc);
  if (!count) {
    await complete(withTx((tx) => tx.objectStore('sessions').delete(id)));
    written = doc;
    return;
  }
  const { stored, blobs } = serialize(doc);
  const fresh = [...blobs].filter(([k]) => !storedBlobs.has(k));
  await makeRoom(fresh.reduce((n, [, b]) => n + b.size, 0));
  const previous = await request(db.transaction('sessions').objectStore('sessions').get(id) as IDBRequest<Session | undefined>);
  const record: Session = {
    id, savedAt: Date.now(), objects: count, unsaved: unsaved(state), blobs: [...blobs.keys()], doc: stored,
    name: (doc.projectName ?? doc.project?.name ?? doc.objects[0]?.name ?? 'Untitled').replace(/\.(stl|obj|3mf)$/i, ''),
    thumbnail: (id === sessionId && thumbnail()) || previous?.thumbnail || null,
  };
  await complete(withTx((tx) => {
    for (const [k, b] of fresh) tx.objectStore('blobs').put(b, k);
    tx.objectStore('sessions').put(record);
  }));
  fresh.forEach(([k]) => storedBlobs.add(k));
  if (id === sessionId) written = doc;
  if (!previous) await prune();
}

function withTx(fill: (tx: IDBTransaction) => void): IDBTransaction {
  const tx = db!.transaction(['sessions', 'blobs'], 'readwrite');
  fill(tx);
  return tx;
}

function save(id = sessionId) {
  clearTimeout(timer);
  const state = getState(), doc = snapshot(state);
  queue = queue.then(() => write(doc, id, state)).catch((e: unknown) => {
    if (warned) return;
    warned = true;
    const quota = e instanceof DOMException && e.name === 'QuotaExceededError';
    toast(quota ? 'Autosave is paused: browser storage is full. Save the project (Ctrl+S) to keep it.' : e instanceof Error ? e.message : `Autosave failed: ${e}`);
  });
  return queue;
}

export const flush = () => save();

export async function start() {
  if (db || typeof indexedDB === 'undefined') return;
  try {
    db = await openDb();
  } catch {
    return;
  }
  (await request(db.transaction('blobs').objectStore('blobs').getAllKeys())).forEach((k) => storedBlobs.add(String(k)));
  subscribeStore(() => {
    if (!sameDoc(written, snapshot())) {
      clearTimeout(timer);
      timer = window.setTimeout(() => void save(), DELAY_MS);
    }
  });
  onLeaveDocument(() => void save());
  subscribeHistory(() => {
    if (documentGeneration() === generation) return;
    generation = documentGeneration();
    sessionId = newKey();
    written = null;
    warned = false;
  });
  window.addEventListener('beforeunload', (e) => {
    void save();
    if (!unsaved()) return;
    e.preventDefault();
    e.returnValue = '';
  });
  document.addEventListener('visibilitychange', () => document.visibilityState === 'hidden' && void save());
  const last = (await sessions())[0];
  if (last && !plateCount(getState())) {
    offer = last;
    setState({ sessionDialog: 'restore' });
  }
}

export async function restore(id: string) {
  if (!db) return;
  if (id !== sessionId) await save();
  const rec = await request(db.transaction('sessions').objectStore('sessions').get(id) as IDBRequest<Session | undefined>);
  if (!rec) return;
  setState({ sessionDialog: null, loading: `Restoring ${rec.name}` });
  try {
    const store = db.transaction('blobs').objectStore('blobs');
    const blobs = new Map(await Promise.all(rec.blobs.map(async (k) => [k, await request(store.get(k) as IDBRequest<File>)] as const)));
    for (const [k, b] of blobs) {
      if (!b) throw new Error('a model file of this session is missing from browser storage');
      blobKeys.set(b, k);
    }
    const meshes = new Map<string, THREE.BufferGeometry>();
    const items = new Map<number, { geometry: THREE.BufferGeometry; centre: THREE.Vector3 }>();
    if (rec.doc.project) {
      let list: { buildIndex: number }[] = [];
      await loadProject(blobs.get(rec.doc.project) as File, {
        items: (it) => { list = it; },
        mesh: (i, geometry, centre) => items.set(list[i].buildIndex, { geometry, centre }),
        missing: () => {},
      });
    }
    for (const o of [...rec.doc.objects, ...rec.doc.projectPlates.flatMap((p) => p.objects)])
      if (!o.project && !meshes.has(o.source)) meshes.set(o.source, await loadMesh(blobs.get(o.source) as File));
    const revive = ({ volumes, ...o }: StoredObject): PlateObject => {
      const source = blobs.get(o.source) as File;
      if (!o.project) return { ...o, source, geometry: meshes.get(o.source)!,
        ...(volumes ? { volumes: volumes.map((v) => ({ ...v, geometry: volumeGeometry(v.geometry.position, v.geometry.index, v.geometry.normal) })) } : {}) };
      const item = o.project.buildIndex === undefined ? undefined : items.get(o.project.buildIndex);
      if (!item) return { ...o, source, geometry: placeholderGeometry() };
      if (!o.project.placeholder) return { ...o, source, geometry: item.geometry };
      const { x, y, z } = item.centre;
      return { ...o, source, geometry: item.geometry, project: { ...o.project, placeholder: false, origin: [x, y, z] },
        instances: [{ ...o.instances[0], x, y, z }, ...o.instances.slice(1)] };
    };
    const objects = rec.doc.objects.map(revive);
    const projectPlates = rec.doc.projectPlates.map((p) => ({ ...p, objects: p.objects.map(revive) }));
    reserveIds(Math.max(0, ...[...objects, ...projectPlates.flatMap((p) => p.objects)].flatMap((o) => [o.id, ...o.instances.map((i) => i.id), ...(o.volumes ?? []).map((v) => v.id)])));
    setState({
      ...rec.doc, objects, projectPlates, project: rec.doc.project ? (blobs.get(rec.doc.project) as File) : null,
      tab: 'prepare', gizmo: 'none', dialog: null, loading: null, job: { state: 'idle', pct: 0, stage: '' },
    });
    newDocument(!rec.unsaved);
    generation = documentGeneration();
    sessionId = rec.id;
    written = snapshot();
  } catch (e) {
    setState({ loading: null });
    toast(`Could not restore ${rec.name}: ${e instanceof Error ? e.message : e}`);
  }
}

export async function remove(id: string) {
  if (!db) return;
  await complete(withTx((tx) => tx.objectStore('sessions').delete(id)));
  if (id === sessionId) written = null;
  await prune();
}
