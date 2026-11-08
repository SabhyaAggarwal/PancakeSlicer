import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { Plugin } from 'vite';

const RESOURCES = fileURLToPath(new URL('../resources', import.meta.url));
const IMAGES = path.join(RESOURCES, 'images');
const PROFILES = path.join(RESOURCES, 'profiles');
const VENDORS = fs.readdirSync(PROFILES).filter(v => fs.existsSync(path.join(PROFILES, `${v}.json`)) && fs.statSync(path.join(PROFILES, v)).isDirectory());
const HEADS = path.join(RESOURCES, 'nonplanar', 'heads');
function headIndex(): string {
  const heads = fs.readdirSync(HEADS).filter((f) => f.endsWith('.json')).map((f) => {
    const h = JSON.parse(fs.readFileSync(path.join(HEADS, f), 'utf8'));
    return { id: String(h.id ?? f.slice(0, -5)), name: String(h.name ?? ''), calibrated: h.calibrated === true };
  });
  heads.sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
  return JSON.stringify(heads);
}

const MIME: Record<string, string> = {
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.stl': 'model/stl',
  '.json': 'application/json',
};

function assetSource(file: string): Buffer | string {
  const data = fs.readFileSync(file);
  if (!file.endsWith('.svg')) return data;
  return data.toString('utf8')
    .replace(/<!DOCTYPE[^>]*>/gi, '')
    .replace(/@font-face\s*\{[^}]*\}/gi, (rule) => /(?:https?:)?\/\//i.test(rule) ? '' : rule);
}

const DARK_COLOURS: [string, string][] = [
  ['#262E30', '#F1F1F1'], ['#323A3D', '#B8B8B8'], ['#808080', '#828282'], ['#CECECE', '#555555'],
  ['#6B6B6B', '#828282'], ['#909090', '#FFFFFF'], ['#F1F1F1', '#383838'],
  ['#DBDBDB', '#4C4C4C'], ['#F0F0F1', '#353535'], ['#515151', '#B8B8B8'],
];

const ACCENT = /#(?:009688|00675B)(?![0-9A-Fa-f])/gi;
const lightIcon = (file: string) => String(assetSource(file)).replace(ACCENT, '#63378F');

function darkIcon(name: string): string | null {
  const own = path.join(IMAGES, `${name}_dark.svg`);
  const file = fs.existsSync(own) ? own : path.join(IMAGES, `${name}.svg`);
  if (!fs.existsSync(file)) return null;
  let svg = String(assetSource(file));
  for (const [from, to] of DARK_COLOURS) svg = svg.replace(new RegExp(`${from}(?![0-9A-Fa-f])`, 'gi'), to);
  return svg.replace(ACCENT, '#A98BD4');
}

function resolveAsset(url: string): string | null {
  let clean: string;
  try { clean = decodeURIComponent(url.split('?')[0]); } catch { return null; }
  let m = /^\/images\/([A-Za-z0-9_.+-]+\.(?:svg|png))$/.exec(clean);
  if (m) return path.join(IMAGES, m[1]);
  m = /^\/nonplanar\/heads\/([a-z0-9_-]+\.json)$/.exec(clean);
  if (m && m[1] !== 'index.json') return path.join(HEADS, m[1]);
  m = /^\/profiles\/([^/\\\x00]+)\/([^/\\\x00]+\.(?:svg|png|stl))$/.exec(clean);
  if (m && m[1] !== '.' && m[1] !== '..') return path.join(PROFILES, m[1], m[2]);
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
        if (req.url?.split('?')[0] === '/nonplanar/heads/index.json') {
          res.setHeader('Content-Type', 'application/json');
          res.end(headIndex());
          return;
        }
        const dark = req.url ? /^\/images\/dark\/([A-Za-z0-9_.+-]+)\.svg$/.exec(req.url.split('?')[0]) : null;
        const darkSvg = dark ? darkIcon(dark[1]) : null;
        if (darkSvg) {
          res.setHeader('Content-Type', MIME['.svg']);
          res.setHeader('Cache-Control', 'max-age=3600');
          res.end(darkSvg);
          return;
        }
        const file = req.url ? resolveAsset(req.url) : null;
        if (!file || !fs.existsSync(file)) return next();
        res.setHeader('Content-Type', MIME[path.extname(file)] ?? 'application/octet-stream');
        res.setHeader('Cache-Control', 'max-age=3600');
        if (file.endsWith('.svg')) res.end(path.dirname(file) === IMAGES ? lightIcon(file) : assetSource(file));
        else fs.createReadStream(file).pipe(res);
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
        if (tokens.has(f.slice(0, -4)) || tokens.has(f)) {
          this.emitFile({ type: 'asset', fileName: `images/${f}`, source: lightIcon(path.join(IMAGES, f)) });
          this.emitFile({ type: 'asset', fileName: `images/dark/${f}`, source: darkIcon(f.slice(0, -4))! });
        }
      }
      this.emitFile({ type: 'asset', fileName: 'nonplanar/heads/index.json', source: headIndex() });
      for (const f of fs.readdirSync(HEADS))
        if (/^[a-z0-9_-]+\.json$/.test(f) && f !== 'index.json')
          this.emitFile({ type: 'asset', fileName: `nonplanar/heads/${f}`, source: fs.readFileSync(path.join(HEADS, f)) });
      for (const f of ['printer_placeholder.png'])
        this.emitFile({ type: 'asset', fileName: `images/${f}`, source: assetSource(path.join(IMAGES, f)) });
      const quoted = (name: string) => new RegExp(`["'\`]${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}["'\`]`).test(text);
      for (const v of VENDORS) {
        const beds = new Set<string>();
        for (const f of fs.existsSync(path.join(PROFILES, v, 'machine')) ? fs.readdirSync(path.join(PROFILES, v, 'machine')) : []) {
          if (!f.endsWith('.json')) continue;
          const m = JSON.parse(fs.readFileSync(path.join(PROFILES, v, 'machine', f), 'utf8'));
          if (m.type === 'machine_model' && quoted(m.name)) for (const b of [m.bed_model, m.bed_texture]) if (b) beds.add(b);
        }
        for (const f of fs.readdirSync(path.join(PROFILES, v))) {
          const cover = /^(.*)_cover\.png$/.exec(f);
          if (!/\.(svg|png|stl)$/.test(f) || !(text.includes(f) || beds.has(f) || cover)) continue;
          this.emitFile({ type: 'asset', fileName: `profiles/${v}/${f}`, source: assetSource(path.join(PROFILES, v, f)) });
        }
      }
    },
  };
}
