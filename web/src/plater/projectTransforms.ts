import type * as THREE from 'three';
import { T3 } from '../three-runtime';
import type { Instance, PlateObject, State } from '../state/store';
import type { PlateJson } from '../api/types';
import { instanceMatrix, plateDescription } from './geometry';
import { toolValue } from '../options';
import { projectPaint } from './paint';

function projectTransform(o: PlateObject, inst: Instance): number[] | undefined {
  if (!o.project?.origin) return undefined;
  const [x, y, z] = o.project.origin;
  const delta = instanceMatrix(o, inst).multiply(new T3.Matrix4().makeTranslation(-x, -y, -z));
  const [px, py] = o.project.plateOrigin ?? [0, 0];
  const e = new T3.Matrix4().makeTranslation(px, py, 0).multiply(delta).multiply(new T3.Matrix4().makeTranslation(-px, -py, 0)).elements;
  return [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14].map((i) => e[i]);
}

export function projectRows(s: Pick<State, 'objects' | 'projectPlates' | 'plateIndex'>): { o: PlateObject; plate: number }[] {
  return s.projectPlates.length
    ? s.projectPlates.flatMap((p) => (p.index === s.plateIndex ? s.objects : p.objects).map((o) => ({ o, plate: p.index })))
    : s.objects.map((o) => ({ o, plate: 1 }));
}

const savedInstance = (o: PlateObject) => o.instances.find((i) => i.id === o.project?.saved?.instanceId) ?? (o.project?.saved ? undefined : o.instances[0]);

export function projectInstances(s: Pick<State, 'objects' | 'projectPlates' | 'plateIndex' | 'projectMaterials'>) {
  const rows = projectRows(s).map((r) => r.o).filter((o) => o.project?.buildIndex !== undefined);
  const count = s.projectMaterials?.builds?.length ?? Math.max(0, ...rows.map((o) => o.project!.buildIndex! + 1));
  const out: { printable: boolean; transform?: number[] }[] = Array.from({ length: count }, () => ({ printable: true }));
  for (const o of rows) {
    const inst = savedInstance(o);
    out[o.project!.buildIndex!] = inst ? { printable: inst.printable !== false, transform: projectTransform(o, inst) } : { printable: true };
  }
  return out;
}

const round = (v: number) => Math.round(v * 1000) / 1000;

export type ProjectEdit =
  | ({ op: 'add_object'; plate: number } & PlateJson['objects'][number])
  | { op: 'add_volume'; object: number; file: string; type: string; name: string; transform: number[]; extruder?: number; settings?: Record<string, string> }
  | { op: 'add_instance'; object: number; x: number; y: number; rot: number; printable: boolean; plate: number }
  | { op: 'delete'; object: number; instance: number }
  | { op: 'set_extruder'; object: number; extruder: number }
  | { op: 'set_paint'; object: number; volume?: number; paint: Record<string, Record<string, string>> };

export function projectEdits(s: Pick<State, 'objects' | 'projectPlates' | 'plateIndex' | 'projectMaterials'>) {
  const opened = s.projectMaterials?.builds ?? [];
  const rows = projectRows(s);
  const added = rows.filter((r) => !r.o.project);
  const plate = plateDescription(added.map((r) => r.o));
  const edits: ProjectEdit[] = plate.objects.map((entry, i) => ({ op: 'add_object', ...entry, plate: added[i].plate }));
  const extruders = new Map<number, number>();
  const present = new Set<string>();
  for (const { o, plate: p } of rows) {
    const saved = o.project?.saved;
    if (!saved) continue;
    if (o.extruder !== saved.extruder) extruders.set(saved.object, o.extruder);
    for (const i of o.instances) {
      if (i.id === saved.instanceId) present.add(`${saved.object}:${saved.instance}`);
      else edits.push({ op: 'add_instance', object: saved.object, x: round(i.x), y: round(i.y), rot: round(saved.rotZ + i.rotZ), printable: i.printable !== false, plate: p });
    }
  }
  for (const [object, extruder] of extruders) edits.push({ op: 'set_extruder', object, extruder });
  const painted = new Set<number>();
  for (const { o } of rows) {
    const saved = o.project?.saved;
    const paint = saved && o.project?.paintable && !painted.has(saved.object) ? projectPaint(o, o.project.paint, o.project.partTris) : [];
    if (paint.length) painted.add(saved!.object);
    for (const p of paint) edits.push({ op: 'set_paint', object: saved!.object, ...p });
  }
  const volumes: { name: string; o: PlateObject; k: number }[] = [];
  for (const { o } of rows) {
    const saved = o.project?.saved;
    if (!saved?.item || !o.project?.origin || !o.volumes?.length) continue;
    const frame = volumeFrame(o);
    o.volumes.forEach((v, k) => {
      const name = `project_${saved.object}_${saved.instance}.v${k + 1}.stl`;
      const m = frame.clone().multiply(new T3.Matrix4().fromArray(v.matrix)).transpose().elements;
      volumes.push({ name, o, k });
      edits.push({ op: 'add_volume', object: saved.object, file: name, type: v.type, name: v.name, transform: Array.from(m, round6),
        ...(v.extruder ? { extruder: v.extruder } : {}),
        ...(Object.keys(v.settings).length ? { settings: Object.fromEntries(Object.entries(v.settings).map(([key, value]) => [key, toolValue(key, value)])) } : {}) });
    });
  }
  for (const b of opened) if (!present.has(`${b.object}:${b.instance}`)) edits.push({ op: 'delete', ...b });
  return { edits, added: added.map((r) => r.o), files: plate.objects.map((e) => e.file), volumes };
}

const round6 = (v: number) => Math.round(v * 1e6) / 1e6;

function volumeFrame(o: PlateObject): THREE.Matrix4 {
  const t = o.project!.saved!.item!, [x, y, z] = o.project!.origin!, [px, py] = o.project!.plateOrigin ?? [0, 0];
  const item = new T3.Matrix4().set(t[0], t[3], t[6], t[9], t[1], t[4], t[7], t[10], t[2], t[5], t[8], t[11], 0, 0, 0, 1);
  const opened = instanceMatrix(o, { id: 0, x, y, z, rotZ: 0 });
  return item.invert().multiply(new T3.Matrix4().makeTranslation(px, py, 0)).multiply(opened);
}
