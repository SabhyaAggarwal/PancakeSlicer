import { createHash, randomBytes } from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';

export interface Meta { id: string; name: string; savedAt: number; revision?: number; objects: number; thumbnail: string | null }
type MetaIn = { name: string; savedAt: number; revision?: number; objects: number };
type StoredProject = { format: number; schemaVersion?: number; meta: MetaIn; doc: unknown };
type Tombstone = { revision: number; deletedAt: number };

export class ProjectConflictError extends Error {
  name = 'ProjectConflictError';
  constructor() { super('This project changed in another tab. Reload latest or keep this version.'); }
}
export class ProjectDeletedError extends Error {
  name = 'ProjectDeletedError';
  constructor() { super('This project was deleted. Export a copy or return to Projects.'); }
}

const ID = /^[A-Za-z0-9._-]{1,64}$/;
const ULID = /^[0-7][0-9A-HJKMNP-TV-Z]{25}$/;
const KEY = /^[A-Za-z0-9._-]{1,128}$/;
const INLINE_MAX = 4096;
const FORMAT = 1;

const TYPED: Record<string, new (b: ArrayBuffer, o: number, n: number) => ArrayBufferView> = {
  Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array,
  Float32Array, Float64Array, BigInt64Array: BigInt64Array as never, BigUint64Array: BigUint64Array as never,
};

export class ProjectStore {
  private readonly root: string;
  private readonly trash: string;
  private readonly tombstones: string;
  private readonly aliasesFile: string;
  private aliases: Record<string, string> = {};
  private readonly chains = new Map<string, Promise<unknown>>();

  constructor(userData: string) {
    this.root = path.join(userData, 'projects');
    this.trash = path.join(userData, 'trash');
    this.tombstones = path.join(userData, 'deleted-projects');
    this.aliasesFile = path.join(userData, 'project-aliases.json');
  }

  async init() {
    const aliases = await readJson<Record<string, string>>(this.aliasesFile);
    this.aliases = Object.fromEntries(Object.entries(aliases ?? {}).filter(([old, id]) => ID.test(old) && !old.startsWith('.') && ULID.test(id)));
  }

  async flush() {
    while (this.chains.size) await Promise.all([...this.chains.values()]);
  }

  private serial<T>(id: string, op: () => Promise<T>): Promise<T> {
    id = this.canonical(id);
    if (!ID.test(id) || id.startsWith('.')) return Promise.reject(new Error(`bad project id: ${id}`));
    const next = (this.chains.get(id) ?? Promise.resolve()).catch(() => undefined).then(op);
    this.chains.set(id, next);
    next.finally(() => { if (this.chains.get(id) === next) this.chains.delete(id); }).catch(() => undefined);
    return next;
  }

  private canonical(id: string) { return Object.hasOwn(this.aliases, id) ? this.aliases[id] : id; }
  private dir(id: string) { return path.join(this.root, this.canonical(id)); }
  private tombstone(id: string) { return path.join(this.tombstones, `${this.canonical(id)}.json`); }
  private blob(id: string, key: string) {
    if (!KEY.test(key) || key.startsWith('.')) throw new Error(`bad blob key: ${key}`);
    return path.join(this.dir(id), 'blobs', key);
  }

  async list(): Promise<Meta[]> {
    const names = await fs.readdir(this.root).catch(() => [] as string[]);
    const metas = await Promise.all(names.filter((n) => ID.test(n) && !n.startsWith('.')).map(async (id) => {
      try {
        if (await exists(this.tombstone(id))) return null;
        const file = JSON.parse(await fs.readFile(path.join(this.dir(id), 'project.json'), 'utf8'));
        return { ...metaOf(id, file.meta), thumbnail: await this.thumbnail(id) };
      } catch {
        return null;
      }
    }));
    return metas.filter((m): m is Meta => m !== null).sort((a, b) => b.savedAt - a.savedAt);
  }

  load(id: string) {
    return this.serial(id, async () => {
      if (await exists(this.tombstone(id))) return null;
      let file;
      try {
        file = JSON.parse(await fs.readFile(path.join(this.dir(id), 'project.json'), 'utf8'));
      } catch (e) {
        if ((e as NodeJS.ErrnoException).code === 'ENOENT') return null;
        throw e;
      }
      const doc = await decode(file.doc, (key) => fs.readFile(this.blob(id, key)));
      return { meta: { ...metaOf(this.canonical(id), file.meta), thumbnail: await this.thumbnail(id) }, doc };
    });
  }

  save(id: string, meta: MetaIn, doc: unknown, thumbnail?: string | null) {
    return this.serial(id, async () => {
      const dir = this.dir(id);
      if (await exists(this.tombstone(id))) throw new ProjectDeletedError();
      const old = await readJson<StoredProject>(path.join(dir, 'project.json'));
      const revision = old?.meta.revision ?? 0;
      if ((meta.revision ?? 0) !== revision) throw new ProjectConflictError();
      await fs.mkdir(path.join(dir, 'blobs'), { recursive: true });
      const used = new Set<string>();
      const encoded = await encode(doc, async (bytes) => {
        const key = `ta-${createHash('sha1').update(bytes).digest('hex')}.bin`;
        used.add(key);
        const file = this.blob(id, key);
        if (!(await exists(file))) await writeAtomic(file, bytes);
        return key;
      });
      const body = { format: FORMAT, schemaVersion: 2,
        meta: { name: String(meta.name), savedAt: Number(meta.savedAt), revision: revision + 1, objects: Number(meta.objects) },
        doc: encoded && typeof encoded === 'object' && !Array.isArray(encoded) ? { ...encoded, schemaVersion: 2 } : encoded };
      await writeAtomic(path.join(dir, 'project.json'), JSON.stringify(body));
      if (thumbnail === null) await fs.rm(path.join(dir, 'thumb.jpg'), { force: true });
      else if (typeof thumbnail === 'string') await writeAtomic(path.join(dir, 'thumb.jpg'), fromDataUrl(thumbnail));
      for (const name of await fs.readdir(path.join(dir, 'blobs'))) {
        if (/^ta-[0-9a-f]{40}\.bin$/.test(name) && !used.has(name)) await fs.rm(path.join(dir, 'blobs', name), { force: true });
      }
    });
  }

  rename(id: string, name: string) {
    return this.serial(id, async () => {
      if (await exists(this.tombstone(id))) throw new ProjectDeletedError();
      const file = path.join(this.dir(id), 'project.json');
      const body = JSON.parse(await fs.readFile(file, 'utf8'));
      body.meta = { ...body.meta, name: String(name), revision: (body.meta.revision ?? 0) + 1 };
      await writeAtomic(file, JSON.stringify(body));
    });
  }

  duplicate(id: string, newId: string, name: string): Promise<Meta> {
    if (this.canonical(newId) === this.canonical(id)) return Promise.reject(new Error('duplicate needs a new id'));
    return this.serial(id, () => this.serial(newId, async () => {
      if (await exists(this.tombstone(id)) || await exists(this.tombstone(newId))) throw new ProjectDeletedError();
      const from = this.dir(id);
      const to = this.dir(newId);
      if (await exists(to)) throw new Error(`project exists: ${newId}`);
      const tmp = path.join(this.root, `.dup-${newId}-${randomBytes(4).toString('hex')}`);
      await fs.mkdir(path.join(tmp, 'blobs'), { recursive: true });
      for (const key of await fs.readdir(path.join(from, 'blobs')).catch(() => [] as string[])) {
        await fs.link(path.join(from, 'blobs', key), path.join(tmp, 'blobs', key))
          .catch(() => fs.copyFile(path.join(from, 'blobs', key), path.join(tmp, 'blobs', key)));
      }
      await fs.copyFile(path.join(from, 'thumb.jpg'), path.join(tmp, 'thumb.jpg')).catch(() => undefined);
      const body = JSON.parse(await fs.readFile(path.join(from, 'project.json'), 'utf8'));
      body.schemaVersion = 2;
      if (body.doc && typeof body.doc === 'object') body.doc.schemaVersion = 2;
      body.meta = { ...body.meta, name: String(name), savedAt: Date.now(), revision: 1 };
      await fs.writeFile(path.join(tmp, 'project.json'), JSON.stringify(body));
      await fs.rename(tmp, to);
      return { ...metaOf(this.canonical(newId), body.meta), thumbnail: await this.thumbnail(newId) };
    }));
  }

  remove(id: string) {
    return this.serial(id, async () => {
      const old = await readJson<StoredProject>(path.join(this.dir(id), 'project.json'));
      const marker = await readJson<Tombstone>(this.tombstone(id));
      const tombstone = { revision: Math.max(old?.meta.revision ?? 0, marker?.revision ?? 0) + 1, deletedAt: Date.now() };
      await fs.mkdir(this.tombstones, { recursive: true });
      await fs.mkdir(this.trash, { recursive: true });
      await writeAtomic(this.tombstone(id), JSON.stringify(tombstone));
      const bin = path.join(this.trash, this.canonical(id));
      await fs.rm(bin, { recursive: true, force: true });
      await fs.rename(this.dir(id), bin).catch((e) => { if (e.code !== 'ENOENT') throw e; });
    });
  }

  restore(id: string) {
    return this.serial(id, async () => {
      const marker = await readJson<Tombstone>(this.tombstone(id));
      const bin = path.join(this.trash, this.canonical(id));
      const from = await exists(bin) ? bin : marker ? this.dir(id) : bin;
      if (from !== this.dir(id) && await exists(this.dir(id))) throw new Error(`project exists: ${id}`);
      const file = path.join(from, 'project.json');
      const body = JSON.parse(await fs.readFile(file, 'utf8'));
      body.meta.revision = Math.max(body.meta.revision ?? 0, marker?.revision ?? 0) + 1;
      await writeAtomic(file, JSON.stringify(body));
      if (from !== this.dir(id)) await fs.rename(from, this.dir(id));
      await fs.rm(this.tombstone(id), { force: true });
    });
  }

  async emptyTrash() {
    await this.flush();
    await fs.rm(this.trash, { recursive: true, force: true });
    await fs.mkdir(this.trash, { recursive: true });
  }

  putBlob(id: string, key: string, data: ArrayBuffer | ArrayBufferView) {
    return this.serial(id, async () => {
      if (await exists(this.tombstone(id))) throw new ProjectDeletedError();
      const file = this.blob(id, key);
      await fs.mkdir(path.dirname(file), { recursive: true });
      const bytes = ArrayBuffer.isView(data) ? Buffer.from(data.buffer, data.byteOffset, data.byteLength) : Buffer.from(data);
      await writeAtomic(file, bytes);
    });
  }

  getBlob(id: string, key: string) {
    return this.serial(id, async () => {
      try {
        const b = await fs.readFile(this.blob(id, key));
        return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
      } catch (e) {
        if ((e as NodeJS.ErrnoException).code === 'ENOENT') return null;
        throw e;
      }
    });
  }

  hasBlob(id: string, key: string) {
    return this.serial(id, async () => exists(this.blob(id, key)));
  }

  private async thumbnail(id: string): Promise<string | null> {
    try {
      const b = await fs.readFile(path.join(this.dir(id), 'thumb.jpg'));
      const mime = b[0] === 0x89 ? 'image/png' : b[0] === 0x52 ? 'image/webp' : 'image/jpeg';
      return `data:${mime};base64,${b.toString('base64')}`;
    } catch {
      return null;
    }
  }
}

function metaOf(id: string, m: Partial<MetaIn> | undefined): Omit<Meta, 'thumbnail'> {
  return { id, name: String(m?.name ?? 'Untitled'), savedAt: Number(m?.savedAt ?? 0), revision: m?.revision ?? 0, objects: Number(m?.objects ?? 0) };
}

async function readJson<T>(file: string): Promise<T | null> {
  try { return JSON.parse(await fs.readFile(file, 'utf8')) as T; }
  catch (e) { if ((e as NodeJS.ErrnoException).code === 'ENOENT') return null; throw e; }
}

const exists = (file: string) => fs.access(file).then(() => true, () => false);

async function writeAtomic(file: string, data: string | Uint8Array) {
  const tmp = `${file}.tmp-${process.pid}-${randomBytes(4).toString('hex')}`;
  const h = await fs.open(tmp, 'w');
  try {
    await h.writeFile(data);
    await h.sync();
  } finally {
    await h.close();
  }
  await fs.rename(tmp, file);
}

function fromDataUrl(url: string): Buffer {
  const m = /^data:[^;,]*(;base64)?,(.*)$/s.exec(url);
  if (!m) throw new Error('thumbnail is not a data URL');
  return m[1] ? Buffer.from(m[2], 'base64') : Buffer.from(decodeURIComponent(m[2]));
}

async function encode(v: unknown, put: (bytes: Buffer) => Promise<string>): Promise<unknown> {
  if (v === null || typeof v !== 'object') return v;
  if (v instanceof ArrayBuffer || ArrayBuffer.isView(v)) {
    const type = v instanceof ArrayBuffer ? 'ArrayBuffer' : v.constructor.name;
    if (type !== 'ArrayBuffer' && !(type in TYPED)) throw new Error(`cannot store ${type}`);
    const bytes = v instanceof ArrayBuffer ? Buffer.from(v) : Buffer.from(v.buffer, v.byteOffset, v.byteLength);
    return bytes.byteLength > INLINE_MAX ? { $typed: type, blob: await put(bytes) } : { $typed: type, b64: bytes.toString('base64') };
  }
  if (Array.isArray(v)) return Promise.all(v.map((x) => encode(x, put)));
  if (v instanceof Date) return { $date: v.getTime() };
  const out: Record<string, unknown> = {};
  for (const [k, x] of Object.entries(v)) if (x !== undefined) out[k] = await encode(x, put);
  return out;
}

async function decode(v: unknown, get: (key: string) => Promise<Buffer>): Promise<unknown> {
  if (v === null || typeof v !== 'object') return v;
  if (Array.isArray(v)) return Promise.all(v.map((x) => decode(x, get)));
  const o = v as Record<string, unknown>;
  if (typeof o.$typed === 'string' && (typeof o.blob === 'string' || typeof o.b64 === 'string')) {
    const src = typeof o.blob === 'string' ? await get(o.blob) : Buffer.from(o.b64 as string, 'base64');
    const buf = src.buffer.slice(src.byteOffset, src.byteOffset + src.byteLength) as ArrayBuffer;
    if (o.$typed === 'ArrayBuffer') return buf;
    const T = TYPED[o.$typed];
    if (!T) throw new Error(`unknown typed array ${o.$typed}`);
    return new T(buf, 0, buf.byteLength / (T as unknown as { BYTES_PER_ELEMENT: number }).BYTES_PER_ELEMENT);
  }
  if (typeof o.$date === 'number' && Object.keys(o).length === 1) return new Date(o.$date);
  const out: Record<string, unknown> = {};
  for (const [k, x] of Object.entries(o)) out[k] = await decode(x, get);
  return out;
}
