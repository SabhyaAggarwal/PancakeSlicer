import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { Plugin } from 'vite';

const RESOURCES = fileURLToPath(new URL('../resources', import.meta.url));
const IMAGES = path.join(RESOURCES, 'images');
const PROFILES = path.join(RESOURCES, 'profiles');
const VENDORS = ['BBL', 'Prusa'];

const MIME: Record<string, string> = {
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.stl': 'model/stl',
};

function resolveAsset(url: string): string | null {
  const clean = decodeURIComponent(url.split('?')[0]);
  let m = /^\/images\/([A-Za-z0-9_.+-]+\.(?:svg|png))$/.exec(clean);
  if (m) return path.join(IMAGES, m[1]);
  m = /^\/profiles\/([A-Za-z0-9_-]+)\/([A-Za-z0-9 _.+-]+\.(?:svg|png|stl))$/.exec(clean);
  if (m) return path.join(PROFILES, m[1], m[2]);
  return null;
}

const MOVES_DIR = process.env.ORCA_MOVES_DIR ?? 'samples-moves';
const SAMPLE = process.env.ORCA_MOVES_SAMPLE ?? path.join(MOVES_DIR, 'dome-a1mini/dome.moves');
const RECORD = process.env.ORCA_RECORD_SAMPLE ?? path.join(MOVES_DIR, 'dome-a1mini/dome.json');

function resolveMoves(url: string): string | null {
  const clean = decodeURIComponent(url.split('?')[0]);
  if (clean === '/__moves/sample') return SAMPLE;
  if (clean === '/__moves/record') return RECORD;
  if (!clean.startsWith('/__moves/')) return null;
  const file = path.resolve(MOVES_DIR, clean.slice('/__moves/'.length));
  return file.startsWith(path.resolve(MOVES_DIR) + path.sep) ? file : null;
}

export function orcaAssets(): Plugin {
  return {
    name: 'orca-assets',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const moves = req.url ? resolveMoves(req.url) : null;
        if (moves) {
          if (!fs.existsSync(moves) || !fs.statSync(moves).isFile()) return next();
          res.setHeader('Content-Type', moves.endsWith('.json') ? 'application/json' : 'application/octet-stream');
          res.setHeader('Content-Length', String(fs.statSync(moves).size));
          fs.createReadStream(moves).pipe(res);
          return;
        }
        const file = req.url ? resolveAsset(req.url) : null;
        if (!file || !fs.existsSync(file)) return next();
        res.setHeader('Content-Type', MIME[path.extname(file)] ?? 'application/octet-stream');
        res.setHeader('Cache-Control', 'max-age=3600');
        fs.createReadStream(file).pipe(res);
      });
    },
    configurePreviewServer(server) {
      server.middlewares.use((req, res, next) => {
        const moves = req.url ? resolveMoves(req.url) : null;
        if (!moves || !fs.existsSync(moves) || !fs.statSync(moves).isFile()) return next();
        res.setHeader('Content-Type', moves.endsWith('.json') ? 'application/json' : 'application/octet-stream');
        res.setHeader('Content-Length', String(fs.statSync(moves).size));
        fs.createReadStream(moves).pipe(res);
      });
    },
    generateBundle(_opts, bundle) {
      const text = Object.values(bundle)
        .map((c) => (c.type === 'chunk' ? c.code : typeof c.source === 'string' ? c.source
          : c.fileName.endsWith('.json') ? Buffer.from(c.source).toString('utf8') : ''))
        .join('\n');
      const tokens = new Set(Array.from(text.matchAll(/["'`]([A-Za-z0-9_.+-]{2,80})["'`]/g), (m) => m[1]));
      for (const f of fs.readdirSync(IMAGES)) {
        if (!f.endsWith('.svg')) continue;
        if (tokens.has(f.slice(0, -4)) || tokens.has(f))
          this.emitFile({ type: 'asset', fileName: `images/${f}`, source: fs.readFileSync(path.join(IMAGES, f)) });
      }
      for (const f of ['printer_placeholder.png'])
        this.emitFile({ type: 'asset', fileName: `images/${f}`, source: fs.readFileSync(path.join(IMAGES, f)) });
      for (const v of VENDORS) {
        for (const f of fs.readdirSync(path.join(PROFILES, v))) {
          const cover = /^(.*)_cover\.png$/.exec(f);
          if (!/\.(svg|png|stl)$/.test(f) || !(text.includes(f) || (cover && new RegExp(`["'\`]${cover[1].replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}["'\`]`).test(text)))) continue;
          this.emitFile({ type: 'asset', fileName: `profiles/${v}/${f}`, source: fs.readFileSync(path.join(PROFILES, v, f)) });
        }
      }
    },
  };
}
