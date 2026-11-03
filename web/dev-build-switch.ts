import type { IncomingMessage, ServerResponse } from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import path from 'node:path';
import { build, type Plugin } from 'vite';

const COOKIE = 'orca-build';
const ON = new RegExp(`(?:^|;\\s*)${COOKIE}=on(?:;|$)`);
const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp',
  '.woff2': 'font/woff2', '.wasm': 'application/wasm', '.txt': 'text/plain; charset=utf-8', '.stl': 'model/stl',
};

export function devBuildSwitch(): Plugin {
  return {
    name: 'orca-dev-build-switch',
    apply: 'serve',
    configureServer(server) {
      const dir = path.join(server.config.cacheDir, 'build');
      let building: Promise<unknown> | null = null;
      const rebuild = () => building ??= build({
        configFile: server.config.configFile, root: server.config.root, mode: 'production', logLevel: 'warn',
        build: { outDir: dir, emptyOutDir: true },
      }).finally(() => { building = null; });

      const redirect = (res: ServerResponse, location: string, cookie: string) => {
        res.writeHead(302, { Location: location, 'Set-Cookie': `${COOKIE}=${cookie}; Path=/; SameSite=Lax`, 'Cache-Control': 'no-store' });
        res.end();
      };

      async function send(req: IncomingMessage, res: ServerResponse, file: string) {
        const info = await stat(file).catch(() => null);
        if (!info?.isFile()) return false;
        const etag = `W/"${info.size.toString(16)}-${info.mtimeMs.toString(16)}"`;
        res.setHeader('Content-Type', TYPES[path.extname(file)] ?? 'application/octet-stream');
        res.setHeader('Cache-Control', file.startsWith(path.join(dir, 'assets') + path.sep) ? 'public, max-age=31536000, immutable' : 'no-cache');
        res.setHeader('ETag', etag);
        if (req.headers['if-none-match'] === etag) { res.statusCode = 304; res.end(); return true; }
        res.setHeader('Content-Length', info.size);
        if (req.method === 'HEAD') res.end();
        else createReadStream(file).pipe(res);
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
