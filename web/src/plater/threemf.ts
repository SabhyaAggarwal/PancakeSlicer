import { unzip, strFromU8 } from 'fflate';
import type { Config } from '../api/types';

export interface ProjectPlate { index: number; name: string; origin: [number, number]; settings: Config;  locked?: boolean }

export interface ProjectMaterials {
  plates: ProjectPlate[];
  config: Config;
  colours: string[];
  matrix: number[] | null;
  multiplier: number | null;
  hasConfig: boolean;
  builds: { object: number; instance: number }[];
}

export interface ProjectItem {
  id: string;
  instance: number;
  buildIndex: number;
  plateIndex: number;
  name: string;
  extruder: number;
  printable: boolean;
  settings: Config;
  origin: [number, number, number];
  rotZ: number;
  transform: number[];
  parts: number;
  paint?: ArchivePaint;
  partTris?: number[];
}

export type ArchivePaint = Partial<Record<'support' | 'seam' | 'fuzzy' | 'color', Record<string, string>>>;
const PAINT_ATTRS = [['support', 'paint_supports'], ['seam', 'paint_seam'], ['fuzzy', 'paint_fuzzy_skin'], ['color', 'paint_color']] as const;

interface Obj {
  name?: string;
  vertices?: Float32Array;
  triangles?: Uint32Array;
  paint?: ArchivePaint;
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
      const tRe = /<triangle\s+v1="(\d+)"\s+v2="(\d+)"\s+v3="(\d+)"([^>]*)>/g;
      for (let t; (t = tRe.exec(tText)); ) {
        if (t[4].includes('paint_')) for (const [kind, key] of PAINT_ATTRS) {
          const value = attr(t[4], key);
          if (value) ((obj.paint ??= {})[kind] ??= {})[String(tris.length / 3)] = value;
        }
        tris.push(+t[1], +t[2], +t[3]);
      }
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

function parseSettings(text: string | undefined): Map<string, { name?: string; extruder?: number; settings: Config; volumes: Set<string> }> {
  const out = new Map<string, { name?: string; extruder?: number; settings: Config; volumes: Set<string> }>();
  if (!text) return out;
  const re = /<object\s+id="([^"]+)">([\s\S]*?)<\/object>/g;
  for (let m; (m = re.exec(text)); ) {
    const own = m[2].split(/<part\b/)[0];
    const meta = (key: string) => new RegExp(`<metadata\\s+key="${key}"\\s+value="([^"]*)"`).exec(own)?.[1];
    const settings: Config = {};
    for (const v of own.matchAll(/<metadata\s+key="([^"]+)"\s+value="([^"]*)"/g))
      if (v[1] !== 'name' && v[1] !== 'extruder') settings[v[1]] = unescapeXml(v[2]);
    const volumes = new Set([...m[2].matchAll(/<part\s+id="([^"]+)"\s+subtype="([^"]+)"/g)].filter((p) => p[2] !== 'normal_part').map((p) => p[1]));
    out.set(m[1], { name: meta('name'), extruder: Number(meta('extruder')) || undefined, settings, volumes });
  }
  return out;
}

const unescapeXml = (s: string) => s.replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');

export async function readProject(file: File, partsOnly = false): Promise<{ items: ProjectItem[]; materials: ProjectMaterials; mesh(i: number): Float32Array | null }> {
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
    const skip = partsOnly && depth === 0 ? settings.get(id)?.volumes : undefined;
    for (const c of o.components)
      if (!skip?.has(c.objectid)) collect(c.path ?? path, c.objectid, compose(c.transform, t), out, depth + 1);
  };

  const leaves = (path: string, id: string, out: Obj[] = [], depth = 0): Obj[] => {
    const o = model(path).objects.get(id);
    if (!o || depth > 8) return out;
    if (o.vertices && o.triangles) out.push(o);
    for (const c of o.components) leaves(c.path ?? path, c.objectid, out, depth + 1);
    return out;
  };

  const configBytes = entry('Metadata/project_settings.config');
  const config = configBytes ? JSON.parse(strFromU8(configBytes)) : {};
  const metadata = entry('Metadata/model_settings.config');
  const plateText = metadata ? strFromU8(metadata) : '';
  const membership = new Map<string, number>();
  const plates: ProjectPlate[] = [];
  for (const p of plateText.matchAll(/<plate\b[^>]*>([\s\S]*?)<\/plate>/g)) {
    const own = p[1].split(/<model_instance\b/)[0];
    const values: Config = {};
    for (const m of own.matchAll(/<metadata\b([^>]*)\/?>/g)) {
      const key = attr(m[1], 'key'), value = attr(m[1], 'value');
      if (key && value !== undefined) values[key] = unescapeXml(value);
    }
    const index = Number(values.plater_id);
    if (!Number.isInteger(index) || index < 1) continue;
    const settings = { ...values };
    if (settings.bed_type && settings.bed_type !== 'Default Plate') settings.curr_bed_type = settings.bed_type;
    delete settings.bed_type;
    for (const k of ['plater_id', 'plater_name', 'locked']) delete settings[k];
    plates.push({ index, name: String(values.plater_name ?? ''), origin: [0, 0], settings, ...(values.locked === 'true' ? { locked: true } : {}) });
    for (const inst of p[1].matchAll(/<model_instance\b[^>]*>([\s\S]*?)<\/model_instance>/g)) {
      const v = (key: string) => new RegExp(`<metadata\\s+key="${key}"\\s+value="([^"]*)"`).exec(inst[1])?.[1];
      membership.set(`${v('object_id')}:${v('instance_id')}`, index);
    }
  }
  if (!plates.length) plates.push({ index: 1, name: '', origin: [0, 0], settings: {} });
  plates.sort((a, b) => a.index - b.index);
  const points = (Array.isArray(config.printable_area) ? config.printable_area : String(config.printable_area ?? '').split(',')).map((s: string) => s.split('x').map(Number));
  const width = Math.max(...points.map((p: number[]) => p[0])) - Math.min(...points.map((p: number[]) => p[0]));
  const depth = Math.max(...points.map((p: number[]) => p[1])) - Math.min(...points.map((p: number[]) => p[1]));
  const columns = Math.ceil(Math.sqrt(Math.max(...plates.map((p) => p.index))));
  for (const p of plates) p.origin = [((p.index - 1) % columns) * width * 1.2 || 0, -Math.floor((p.index - 1) / columns) * depth * 1.2 || 0];
  const counts = new Map<string, number>();
  const build = root.build;
  const items = build.map((b, i) => {
    const meta = settings.get(b.objectid);
    const instance = counts.get(b.objectid) ?? 0;
    counts.set(b.objectid, instance + 1);
    const plateIndex = membership.get(`${b.objectid}:${instance}`) ?? (plates.length === 1 ? plates[0].index : 0);
    if (!plateIndex) throw new Error('Cannot resolve saved plate membership for every build item');
    const plate = plates.find((p) => p.index === plateIndex);
    return {
      id: b.objectid, instance, buildIndex: i, plateIndex,
      printable: b.printable,
      settings: meta?.settings ?? {},
      name: unescapeXml(meta?.name ?? root.objects.get(b.objectid)?.name ?? `Object ${i + 1}`),
      extruder: meta?.extruder ?? 1,
      origin: [b.transform[9] - (plate?.origin[0] ?? 0), b.transform[10] - (plate?.origin[1] ?? 0), b.transform[11]] as [number, number, number],
      rotZ: Math.atan2(b.transform[1], b.transform[0]) * 180 / Math.PI,
      transform: loadedItem(b),
      ...volumesOf(b.objectid),
    };
  });
  function volumesOf(id: string): { parts: number; paint?: ArchivePaint; partTris?: number[] } {
    const list = leaves(rootPath, id);
    if (list.length === 1) return { parts: 1, ...(list[0].paint ? { paint: list[0].paint } : {}) };
    const nonParts = settings.get(id)?.volumes;
    const comps = root.objects.get(id)?.components ?? [];
    if (!list.length || root.objects.get(id)?.vertices || comps.length !== list.length || comps.some((c) => nonParts?.has(c.objectid)))
      return { parts: list.length };
    const paint: ArchivePaint = {};
    let offset = 0;
    for (const leaf of list) {
      for (const [kind, map] of Object.entries(leaf.paint ?? {}) as [keyof ArchivePaint, Record<string, string>][])
        for (const [k, v] of Object.entries(map)) (paint[kind] ??= {})[String(Number(k) + offset)] = v;
      offset += leaf.triangles!.length / 3;
    }
    return { parts: list.length, partTris: list.map((leaf) => leaf.triangles!.length / 3), ...(Object.keys(paint).length ? { paint } : {}) };
  }
  function loadedItem(b: { objectid: string; transform: number[] }) {
    const comps = root.objects.get(b.objectid)?.components ?? [];
    if (comps.length !== 1) return b.transform;
    const t = compose(comps[0].transform, b.transform);
    return [...b.transform.slice(0, 9), t[9], t[10], t[11]];
  }
  const mesh = (i: number) => {
    const tris: number[] = [];
    collect(rootPath, build[i].objectid, build[i].transform, tris);
    const origin = plates.find((p) => p.index === items[i].plateIndex)?.origin ?? [0, 0];
    for (let k = 0; k < tris.length; k += 3) { tris[k] -= origin[0]; tris[k + 1] -= origin[1]; }
    return tris.length ? new Float32Array(tris) : null;
  };
  const numbers = (v: unknown): number[] | null => {
    const list = Array.isArray(v) ? v.map(Number) : typeof v === 'string' ? v.split(/[,;]/).map(Number) : [];
    return list.length && list.every(Number.isFinite) ? list : null;
  };
  const colourValue = config.filament_colour;
  const colours = (Array.isArray(colourValue) ? colourValue : typeof colourValue === 'string' ? colourValue.split(';') : [])
    .map(String);
  const materials = { plates, config, colours, matrix: numbers(config.flush_volumes_matrix), multiplier: numbers(config.flush_multiplier)?.[0] ?? null,
    hasConfig: !!configBytes || !!entry('Metadata/Slic3r_PE.config'), builds: items.map((it) => ({ object: Number(it.id), instance: it.instance })) };
  return { items, materials, mesh };
}
