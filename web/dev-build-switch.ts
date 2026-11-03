import type { IncomingMessage, ServerResponse } from 'node:http';
import { createReadStream } from 'node:fs';
import { readdir, readFile, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { promisify } from 'node:util';
import { brotliCompress, constants } from 'node:zlib';
import type { Plugin } from 'vite';

const COOKIE = 'orca-build';
const ON = new RegExp(`(?:^|;\\s*)${COOKIE}=on(?:;|$)`);
const PRECOMPRESSED = /\.(js|css|json|svg|wasm|txt|stl)$/;
const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp',
  '.woff2': 'font/woff2', '.wasm': 'application/wasm', '.txt': 'text/plain; charset=utf-8', '.stl': 'model/stl',
};

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

export function devBuildSwitch(): Plugin {
  return {
    name: 'orca-dev-build-switch',
    apply: 'serve',
    configureServer(server) {
      const dir = path.join(server.config.cacheDir, 'build');
      let building: Promise<unknown> | null = null;
      const rebuild = () => building ??= new Promise<void>((resolve, reject) => {
        const child = spawn(process.execPath, [path.join(server.config.root, 'node_modules/vite/bin/vite.js'), 'build',
          '--config', server.config.configFile!, '--mode', 'production', '--logLevel', 'warn', '--outDir', dir, '--emptyOutDir'],
        { cwd: server.config.root, env: { ...process.env, NODE_ENV: 'production' }, stdio: ['ignore', 'ignore', 'pipe'] });
        let stderr = '';
        child.stderr.on('data', (chunk: Buffer) => { stderr += chunk; });
        child.on('error', reject);
        child.on('close', (code) => code === 0 ? resolve() : reject(new Error(stderr.slice(-4000) || `vite build exited ${code}`)));
      }).then(() => precompress(dir)).finally(() => { building = null; });

      const redirect = (res: ServerResponse, location: string, cookie: string) => {
        res.writeHead(302, { Location: location, 'Set-Cookie': `${COOKIE}=${cookie}; Path=/; SameSite=Lax`, 'Cache-Control': 'no-store' });
        res.end();
      };

      async function send(req: IncomingMessage, res: ServerResponse, file: string) {
        const info = await stat(file).catch(() => null);
        if (!info?.isFile()) return false;
        const br = PRECOMPRESSED.test(file) && /\bbr\b/.test(String(req.headers['accept-encoding'] ?? ''))
          ? await stat(file + '.br').catch(() => null) : null;
        const etag = `W/"${info.size.toString(16)}-${info.mtimeMs.toString(16)}${br ? '-br' : ''}"`;
        res.setHeader('Content-Type', TYPES[path.extname(file)] ?? 'application/octet-stream');
        res.setHeader('Cache-Control', file.startsWith(path.join(dir, 'assets') + path.sep) ? 'public, max-age=31536000, immutable' : 'no-cache');
        res.setHeader('ETag', etag);
        if (PRECOMPRESSED.test(file)) res.setHeader('Vary', 'Accept-Encoding');
        if (req.headers['if-none-match'] === etag) { res.statusCode = 304; res.end(); return true; }
        if (br) res.setHeader('Content-Encoding', 'br');
        res.setHeader('Content-Length', br ? br.size : info.size);
        if (req.method === 'HEAD') res.end();
        else createReadStream(br ? file + '.br' : file).pipe(res);
        return true;
      }

      server.middlewares.use((req, res, next) => {
        if (req.method !== 'GET' && req.method !== 'HEAD') return next();
        const url = new URL(req.url ?? '/', 'http://dev');
        const flag = url.searchParams.get('build');
        if (flag === 'on' || flag === 'off') {
          url.searchParams.delete('build');
          const location = url.pathname + url.search;
          if (flag === 'off') return redirect(res, location, 'off; Max-Age=0');
          rebuild().then(() => redirect(res, location, `on; Max-Age=${30 * 86400}`), (error: unknown) => {
            res.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' });
            res.end(`The build failed; the dev server is unchanged.\n\n${error instanceof Error ? error.message : String(error)}`);
          });
          return;
        }
        if (!ON.test(req.headers.cookie ?? '') || url.pathname.startsWith('/api/')) return next();
        const file = path.join(dir, path.normalize(decodeURIComponent(url.pathname)));
        if (!file.startsWith(dir + path.sep)) return next();
        void send(req, res, file).then((sent) => sent || (!path.extname(url.pathname) && send(req, res, path.join(dir, 'index.html'))))
          .then((sent) => { if (!sent) next(); }, next);
      });
    },
  };
}
