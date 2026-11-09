
import { createHash } from 'node:crypto';
import { constants, type BigIntStats } from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';

const ROOTS = ['projects', 'trash', 'deleted-projects', 'project-aliases.json', 'assets'];
const MAX_CHUNK = 4 * 1024 * 1024;
export interface LegacyFile { path: string; size: number; sha256: string; mode: number; mtimeNs: string }
export interface LegacyWorkspaceInventory {
  version: 1;
  complete: true;
  source: 'electron-user-data';
  fingerprint: string;
  roots: { name: string; paths: string[] }[];
  directories: string[];
  files: LegacyFile[];
}
export interface LegacyRead { fingerprint: string; path: string; offset: number; length: number }
const stamp = (s: BigIntStats) => [s.dev, s.ino, s.size, s.mode, s.mtimeNs, s.ctimeNs].join(':');
const incomplete = (reason: string) => new Error(`Oops! We couldn't finish importing your older desktop workspace: ${reason}`);

export class LegacyWorkspaceSource {
  private snapshot: { fingerprint: string; stamps: Map<string, string>; files: Map<string, LegacyFile> } | null = null;
  private scanning: Promise<LegacyWorkspaceInventory> | null = null;
  constructor(private readonly userData: string) {}

  inventory(): Promise<LegacyWorkspaceInventory> {

    if (this.scanning) return this.scanning;
    this.snapshot = null;
    const scan = this.scan();
    this.scanning = scan;
    void scan.finally(() => { this.scanning = null; }).catch(() => undefined);
    return scan;
  }

  private async scan(): Promise<LegacyWorkspaceInventory> {

    const names = (await fs.readdir(this.userData)).sort();
    const roots = ROOTS.map((name) => ({ name, paths: names.filter((n) => n.toLowerCase() === name) }));
    const files: LegacyFile[] = [], directories: string[] = [], stamps = new Map<string, string>();
    const visit = async (relative: string): Promise<void> => {
      const full = path.join(this.userData, relative);
      const before = await fs.lstat(full, { bigint: true });
      if (before.isSymbolicLink()) throw incomplete(`symbolic link ${relative}; preserve source for recovery`);
      if (before.isDirectory()) {
        directories.push(relative);
        for (const name of (await fs.readdir(full)).sort()) await visit(`${relative}/${name}`);
        if (stamp(before) !== stamp(await fs.lstat(full, { bigint: true }))) throw incomplete(`directory changed: ${relative}`);
        return;
      }
      if (!before.isFile() || before.size > BigInt(Number.MAX_SAFE_INTEGER)) throw incomplete(`unsupported file ${relative}`);
      const handle = await this.open(relative);
      try {
        if (stamp(before) !== stamp(await handle.stat({ bigint: true }))) throw incomplete(`file changed: ${relative}`);
        const hash = createHash('sha256');
        for await (const chunk of handle.createReadStream({ autoClose: false })) hash.update(chunk);
        if (stamp(before) !== stamp(await handle.stat({ bigint: true }))) throw incomplete(`file changed: ${relative}`);
        files.push({ path: relative, size: Number(before.size), sha256: hash.digest('hex'), mode: Number(before.mode), mtimeNs: String(before.mtimeNs) });
        stamps.set(relative, stamp(before));
      } finally { await handle.close(); }
    };
    for (const root of roots) for (const name of root.paths) await visit(name);
    const currentNames = (await fs.readdir(this.userData)).sort();
    for (const root of roots) {
      if (JSON.stringify(root.paths) !== JSON.stringify(currentNames.filter((n) => n.toLowerCase() === root.name))) throw incomplete('root entries changed');
    }
    files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
    directories.sort();
    const body = { version: 1 as const, complete: true as const, source: 'electron-user-data' as const, roots, directories, files };
    const fingerprint = createHash('sha256').update(JSON.stringify(body)).digest('hex');
    this.snapshot = { fingerprint, stamps, files: new Map(files.map((file) => [file.path, file])) };
    return { ...body, fingerprint };
  }

  async readFile(request: LegacyRead): Promise<Uint8Array> {
    const snapshot = this.snapshot;
    if (!request || !snapshot || request.fingerprint !== snapshot.fingerprint) throw incomplete('inventory fingerprint missing or expired');
    const file = snapshot.files.get(request.path);
    if (!file || !Number.isSafeInteger(request.offset) || !Number.isSafeInteger(request.length)
      || request.offset < 0 || request.length < 0 || request.length > MAX_CHUNK
      || request.offset > file.size || request.length > file.size - request.offset) throw incomplete('invalid file range');
    const handle = await this.open(file.path);
    try {
      const expected = snapshot.stamps.get(file.path);
      if (stamp(await handle.stat({ bigint: true })) !== expected) throw incomplete(`file changed: ${file.path}`);
      const bytes = new Uint8Array(request.length);
      let offset = 0;
      while (offset < bytes.length) {
        const read = await handle.read(bytes, offset, bytes.length - offset, request.offset + offset);
        if (!read.bytesRead) throw incomplete(`short read: ${file.path}`);
        offset += read.bytesRead;
      }
      if (stamp(await handle.stat({ bigint: true })) !== expected) throw incomplete(`file changed: ${file.path}`);
      return bytes;
    } finally { await handle.close(); }
  }

  private async open(relative: string) {
    const segments = relative.split('/');
    if (segments.some((s) => !s || s === '.' || s === '..' || s.includes('\\')) || !ROOTS.includes(segments[0].toLowerCase())) throw incomplete('invalid source path');

    for (let i = 1; i < segments.length; i++) {
      const stat = await fs.lstat(path.join(this.userData, ...segments.slice(0, i)));
      if (!stat.isDirectory() || stat.isSymbolicLink()) throw incomplete(`source ancestor changed: ${relative}`);
    }
    return fs.open(path.join(this.userData, ...segments), constants.O_RDONLY | constants.O_NOFOLLOW);
  }
}
