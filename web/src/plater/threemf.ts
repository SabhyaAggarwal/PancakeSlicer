import { unzip, strFromU8 } from 'fflate';

export interface ProjectMaterials {
  colours: string[];
  matrix: number[] | null;
  multiplier: number | null;
}

export interface ProjectItem {
  id: string;
  name: string;
  extruder: number;
  origin: [number, number, number];
}

interface Obj {
  name?: string;
  vertices?: Float32Array;
  triangles?: Uint32Array;
  components: { objectid: string; path?: string; transform: number[] }[];
}

const IDENTITY = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0];

function parseTransform(t: string | undefined): number[] {
  const v = (t ?? '').trim().split(/\s+/).map(Number);
  return v.length === 12 && v.every(Number.isFinite) ? v : IDENTITY;
}

function compose(a: number[], b: number[]): number[] {
  const out = new Array(12).fill(0);
  for (let r = 0; r < 4; r++)
    for (let c = 0; c < 3; c++) {
      let s = r === 3 ? b[9 + c] : 0;
      for (let k = 0; k < 3; k++) s += a[r * 3 + k] * b[k * 3 + c];
      out[r * 3 + c] = s;
    }
  return out;
}

const attr = (tag: string, name: string) => new RegExp(`\\s${name}="([^"]*)"`).exec(tag)?.[1];

function parseModel(text: string): { objects: Map<string, Obj>; build: { objectid: string; transform: number[]; printable: boolean }[] } {
  const objects = new Map<string, Obj>();
  const objRe = /<object\b([^>]*)>([\s\S]*?)<\/object>/g;
  for (let m; (m = objRe.exec(text)); ) {
    const head = m[1], body = m[2];
    const id = attr(head, 'id');
    if (!id) continue;
    const obj: Obj = { name: attr(head, 'name'), components: [] };
    const vStart = body.indexOf('<vertices');
    if (vStart >= 0) {
      const vEnd = body.indexOf('</vertices>', vStart);
      const vText = body.slice(vStart, vEnd);
      const verts: number[] = [];
      const vRe = /<vertex\s+x="([^"]+)"\s+y="([^"]+)"\s+z="([^"]+)"/g;
      for (let v; (v = vRe.exec(vText)); ) verts.push(+v[1], +v[2], +v[3]);
      const tText = body.slice(body.indexOf('<triangles', vEnd), body.indexOf('</triangles>', vEnd));
      const tris: number[] = [];
      const tRe = /<triangle\s+v1="(\d+)"\s+v2="(\d+)"\s+v3="(\d+)"/g;
      for (let t; (t = tRe.exec(tText)); ) tris.push(+t[1], +t[2], +t[3]);
      obj.vertices = new Float32Array(verts);
      obj.triangles = new Uint32Array(tris);
    }
    const cRe = /<component\b([^>]*)\/?>/g;
    for (let c; (c = cRe.exec(body)); ) {
      const objectid = attr(c[1], 'objectid');
      if (objectid) obj.components.push({ objectid, path: attr(c[1], 'p:path'), transform: parseTransform(attr(c[1], 'transform')) });
    }
    objects.set(id, obj);
  }
  const build: { objectid: string; transform: number[]; printable: boolean }[] = [];
  const buildText = /<build\b[^>]*>([\s\S]*?)<\/build>/.exec(text)?.[1] ?? '';
  const iRe = /<item\b([^>]*)\/?>/g;
  for (let i; (i = iRe.exec(buildText)); ) {
    const objectid = attr(i[1], 'objectid');
    if (objectid) build.push({ objectid, transform: parseTransform(attr(i[1], 'transform')), printable: attr(i[1], 'printable') !== '0' });
  }
  return { objects, build };
}

function parseSettings(text: string | undefined): Map<string, { name?: string; extruder?: number }> {
  const out = new Map<string, { name?: string; extruder?: number }>();
  if (!text) return out;
  const re = /<object\s+id="([^"]+)">([\s\S]*?)<\/object>/g;
  for (let m; (m = re.exec(text)); ) {
    const own = m[2].split(/<part\b/)[0];
    const meta = (key: string) => new RegExp(`<metadata\\s+key="${key}"\\s+value="([^"]*)"`).exec(own)?.[1];
    out.set(m[1], { name: meta('name'), extruder: Number(meta('extruder')) || undefined });
  }
  return out;
}

const unescapeXml = (s: string) => s.replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');

export async function readProject(file: File): Promise<{ items: ProjectItem[]; materials: ProjectMaterials; mesh(i: number): Float32Array | null }> {
  const bytes = new Uint8Array(await file.arrayBuffer());
  const zip = await new Promise<Record<string, Uint8Array>>((resolve, reject) => unzip(bytes, (err, data) => (err ? reject(err) : resolve(data))));
  const entry = (p: string) => zip[p.replace(/^\//, '')] ?? Object.entries(zip).find(([k]) => k.toLowerCase() === p.replace(/^\//, '').toLowerCase())?.[1];
  const rels = entry('_rels/.rels');
  const rootPath = (rels && /Target="([^"]+\.model)"/i.exec(strFromU8(rels))?.[1]) || '/3D/3dmodel.model';
  const rootBytes = entry(rootPath);
  if (!rootBytes) throw new Error(`${file.name}: no 3D model part in the archive`);
  const models = new Map<string, ReturnType<typeof parseModel>>();
  const model = (path: string) => {
    let m = models.get(path);
    if (!m) {
      const bytes = entry(path);
      m = bytes ? parseModel(strFromU8(bytes)) : { objects: new Map(), build: [] };
      models.set(path, m);
    }
    return m;
  };
  const root = model(rootPath);
  const settings = parseSettings(entry('Metadata/model_settings.config') && strFromU8(entry('Metadata/model_settings.config')!));

  const collect = (path: string, id: string, t: number[], out: number[], depth = 0) => {
    const o = model(path).objects.get(id);
    if (!o || depth > 8) return;
    if (o.vertices && o.triangles) {
      const v = o.vertices;
      for (const k of o.triangles) {
        const x = v[3 * k], y = v[3 * k + 1], z = v[3 * k + 2];
        out.push(x * t[0] + y * t[3] + z * t[6] + t[9], x * t[1] + y * t[4] + z * t[7] + t[10], x * t[2] + y * t[5] + z * t[8] + t[11]);
      }
    }
    for (const c of o.components) collect(c.path ?? path, c.objectid, compose(c.transform, t), out, depth + 1);
  };

  const build = root.build.filter((b) => b.printable);
  const items = build.map((b, i) => {
    const meta = settings.get(b.objectid);
    return {
      id: b.objectid,
      name: unescapeXml(meta?.name ?? root.objects.get(b.objectid)?.name ?? `Object ${i + 1}`),
      extruder: meta?.extruder ?? 1,
      origin: [b.transform[9], b.transform[10], b.transform[11]] as [number, number, number],
    };
  });
  const mesh = (i: number) => {
    const tris: number[] = [];
    collect(rootPath, build[i].objectid, build[i].transform, tris);
    return tris.length ? new Float32Array(tris) : null;
  };
  const configBytes = entry('Metadata/project_settings.config');
  const config = configBytes ? JSON.parse(strFromU8(configBytes)) : {};
  const numbers = (v: unknown): number[] | null => {
    const list = Array.isArray(v) ? v.map(Number) : typeof v === 'string' ? v.split(/[,;]/).map(Number) : [];
    return list.length && list.every(Number.isFinite) ? list : null;
  };
  const colourValue = config.filament_colour;
  const colours = (Array.isArray(colourValue) ? colourValue : typeof colourValue === 'string' ? colourValue.split(';') : [])
    .map(String);
  const materials = { colours, matrix: numbers(config.flush_volumes_matrix), multiplier: numbers(config.flush_multiplier)?.[0] ?? null };
  return { items, materials, mesh };
}
