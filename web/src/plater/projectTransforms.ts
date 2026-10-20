import * as THREE from 'three';
import type { Instance, PlateObject, State } from '../state/store';
import type { PlateJson } from '../api/types';
import { instanceMatrix, plateDescription } from './geometry';

function projectTransform(o: PlateObject, inst: Instance): number[] | undefined {
  if (!o.project?.origin) return undefined;
  const [x, y, z] = o.project.origin;
  const delta = instanceMatrix(o, inst).multiply(new THREE.Matrix4().makeTranslation(-x, -y, -z));
  const [px, py] = o.project.plateOrigin ?? [0, 0];
  const e = new THREE.Matrix4().makeTranslation(px, py, 0).multiply(delta).multiply(new THREE.Matrix4().makeTranslation(-px, -py, 0)).elements;
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
  | { op: 'add_instance'; object: number; x: number; y: number; rot: number; printable: boolean; plate: number }
  | { op: 'delete'; object: number; instance: number }
  | { op: 'set_extruder'; object: number; extruder: number };

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
  for (const b of opened) if (!present.has(`${b.object}:${b.instance}`)) edits.push({ op: 'delete', ...b });
  return { edits, added: added.map((r) => r.o), files: plate.objects.map((e) => e.file) };
}
