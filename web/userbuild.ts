





import { execFile, spawn } from 'node:child_process';
import { readdir, readFile, rename, rm, stat, symlink, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { promisify } from 'node:util';
import { brotliCompress, constants } from 'node:zlib';

const PRECOMPRESSED = /\.(js|css|json|svg|wasm|txt|stl)$/;

const KEPT_BUILDS = 10;

const [root, branch] = process.argv.slice(2);
const tree = path.join(root, 'tree');
const builds = path.join(root, 'builds');
const git = (...args: string[]) => promisify(execFile)('git', ['-C', tree, ...args]).then((r) => r.stdout.trim());

function run(command: string, args: string[], cwd: string) {
  return new Promise<void>((resolve, reject) => {
    const child = spawn(command, args, { cwd, env: { ...process.env, NODE_ENV: 'production' }, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    child.stdout.on('data', (chunk: Buffer) => { output += chunk; });
    child.stderr.on('data', (chunk: Buffer) => { output += chunk; });
    child.on('error', reject);
    child.on('close', (code) => code === 0 ? resolve() : reject(new Error(`${path.basename(args[0])} exited ${code}\n${output.slice(-4000)}`)));
  });
}



async function precompress(dir: string) {
  const compress = promisify(brotliCompress);
  const files = (await readdir(dir, { recursive: true })).filter((file) => PRECOMPRESSED.test(file));
  await Promise.all(files.map(async (file) => {
    const body = await readFile(path.join(dir, file));
    if (body.length < 1024) return;
    const br = await compress(body, { params: { [constants.BROTLI_PARAM_QUALITY]: 11, [constants.BROTLI_PARAM_SIZE_HINT]: body.length } });
    if (br.length < body.length) await writeFile(path.join(dir, file + '.br'), br);
  }));
}

async function writeAtomic(file: string, body: string) {
  await writeFile(file + '.tmp', body);
  await rename(file + '.tmp', file);
}

async function status() {
  return JSON.parse(await readFile(path.join(builds, 'status.json'), 'utf8').catch(() => '{}'));
}

async function build(sha: string) {
  const started = Date.now();
  await git('checkout', '--detach', '-q', '-f', sha);
  const web = path.join(tree, 'web');
  const out = path.join(builds, sha);
  await rm(out + '.tmp', { recursive: true, force: true });
  await Promise.all([
    run(process.execPath, ['node_modules/typescript/bin/tsc', '--noEmit', '-p', '.'], web),
    run(process.execPath, ['node_modules/vite/bin/vite.js', 'build', '--mode', 'production', '--logLevel', 'warn', '--outDir', out + '.tmp', '--emptyOutDir'], web),
  ]);
  await precompress(out + '.tmp');
  await rm(out, { recursive: true, force: true });
  await rename(out + '.tmp', out);
  await symlink(sha, path.join(builds, 'current.tmp'));
  await rename(path.join(builds, 'current.tmp'), path.join(builds, 'current'));
  const subject = await git('log', '-1', '--format=%s', sha);
  await writeAtomic(path.join(builds, 'status.json'), JSON.stringify({
    current: { sha, subject, builtAt: new Date().toISOString(), buildMs: Date.now() - started },
  }, null, 2));
  console.log(`${new Date().toISOString()} live ${sha.slice(0, 10)} in ${Date.now() - started} ms: ${subject}`);
}

async function prune(current: string) {
  const dirs = await Promise.all((await readdir(builds, { withFileTypes: true }))
    .filter((e) => e.isDirectory() && /^[0-9a-f]{40}$/.test(e.name) && e.name !== current)
    .map(async (e) => ({ name: e.name, mtime: (await stat(path.join(builds, e.name))).mtimeMs })));
  for (const dir of dirs.sort((a, b) => b.mtime - a.mtime).slice(KEPT_BUILDS))
    await rm(path.join(builds, dir.name), { recursive: true, force: true });
}


let attempted = '';
for (;;) {
  const sha = await git('rev-parse', `refs/heads/${branch}`).catch(() => '');
  const live = (await status()).current?.sha;
  if (sha && sha !== live && sha !== attempted) {
    attempted = sha;
    try {
      await build(sha);
      await prune(sha);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      console.error(`${new Date().toISOString()} kept ${String(live).slice(0, 10)}: ${sha.slice(0, 10)} failed\n${message}`);
      await writeAtomic(path.join(builds, 'status.json'), JSON.stringify({ ...await status(), failed: { sha, at: new Date().toISOString(), error: message.slice(-4000) } }, null, 2));
      await rm(path.join(builds, sha + '.tmp'), { recursive: true, force: true });
    }
  }
  await new Promise((resolve) => setTimeout(resolve, 5000));
}
