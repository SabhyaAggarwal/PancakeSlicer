import type { IncomingMessage, ServerResponse } from 'node:http';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { readdir, readFile, readlink, stat } from 'node:fs/promises';
import path from 'node:path';
import type { Plugin } from 'vite';

const COOKIE = 'orca-build';
const COOKIE_VALUE = new RegExp(`(?:^|;\\s*)${COOKIE}=(on|off)(?:;|$)`);
const LOOPBACK = /^(127\.\d+\.\d+\.\d+|localhost|\[::1\])(:\d+)?$/;
const BUILDS = process.env.ORCA_USERBUILD_DIR ?? 'userbuild/builds';
const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp',
  '.woff2': 'font/woff2', '.wasm': 'application/wasm', '.txt': 'text/plain; charset=utf-8', '.stl': 'model/stl',
};

function wantsBuild(req: IncomingMessage) {
  const cookie = COOKIE_VALUE.exec(req.headers.cookie ?? '')?.[1];
  if (cookie) return cookie === 'on';
  const h = req.headers;
  return !LOOPBACK.test(String(h['x-forwarded-host'] ?? h.host ?? '')) || !!(h['x-forwarded-for'] || h['cf-ray']);
}

const digests = new Map<string, Promise<string>>();
const digest = (file: string) => {
  let d = digests.get(file);
  if (!d) {
    d = readFile(file).then((b) => createHash('sha256').update(b).digest('hex').slice(0, 16));
    digests.set(file, d);
    if (digests.size > 4096) digests.delete(digests.keys().next().value!);
  }
  return d;
};

async function send(req: IncomingMessage, res: ServerResponse, sha: string, name: string) {
  const file = path.join(BUILDS, sha, name);
  const info = await stat(file).catch(() => null);
  if (!info?.isFile()) return false;
  const br = /\bbr\b/.test(String(req.headers['accept-encoding'] ?? '')) ? await stat(file + '.br').catch(() => null) : null;
  const etag = `W/"${await digest(file)}-${info.size.toString(16)}${br ? '-br' : ''}"`;
  res.setHeader('Content-Type', TYPES[path.extname(file)] ?? 'application/octet-stream');
  res.setHeader('Cache-Control', name.startsWith('assets' + path.sep) ? 'public, max-age=31536000, immutable' : 'no-cache');
  res.setHeader('ETag', etag);
  res.setHeader('X-Build', sha);
  if (br) res.setHeader('Vary', 'Accept-Encoding');
  if (req.headers['if-none-match'] === etag) { res.statusCode = 304; res.end(); return true; }
  if (br) res.setHeader('Content-Encoding', 'br');
  res.setHeader('Content-Length', br ? br.size : info.size);
  if (req.method === 'HEAD') res.end();
  else createReadStream(br ? file + '.br' : file).pipe(res);
  return true;
}

async function olderBuilds(current: string) {
  const dirs = await Promise.all((await readdir(BUILDS, { withFileTypes: true }).catch(() => []))
    .filter((e) => e.isDirectory() && /^[0-9a-f]{40}$/.test(e.name) && e.name !== current)
    .map(async (e) => ({ name: e.name, mtime: (await stat(path.join(BUILDS, e.name))).mtimeMs })));
  return dirs.sort((a, b) => b.mtime - a.mtime).map((d) => d.name);
}

export function devBuildSwitch(): Plugin {
  return {
    name: 'orca-dev-build-switch',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (req.method !== 'GET' && req.method !== 'HEAD') return next();
        const url = new URL(req.url ?? '/', 'http://dev');
        const flag = url.searchParams.get('build');
        if (flag === 'on' || flag === 'off') {
          url.searchParams.delete('build');
          res.writeHead(302, { Location: url.pathname + url.search, 'Set-Cookie': `${COOKIE}=${flag}; Path=/; Max-Age=${30 * 86400}; SameSite=Lax`, 'Cache-Control': 'no-store' });
          res.end();
          return;
        }
        if (url.pathname.startsWith('/api/') || !wantsBuild(req)) return next();
        const name = path.normalize(decodeURIComponent(url.pathname)).replace(/^[/\\]+/, '');
        if (name.startsWith('..')) return next();
        void (async () => {
          const current = await readlink(path.join(BUILDS, 'current')).catch(() => null);
          if (!current) return false;
          if (await send(req, res, current, name)) return true;
          if (name.startsWith('assets' + path.sep)) {
            for (const sha of await olderBuilds(current)) if (await send(req, res, sha, name)) return true;
            res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store', 'X-Build': current });
            res.end('No build has this asset.');
            return true;
          }
          return !path.extname(name) && send(req, res, current, 'index.html');
        })().then((sent) => { if (!sent) next(); }, next);
      });
    },
  };
}
