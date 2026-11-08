import type { PlateObject, State } from '../state/store';
import type { ProjectItem } from './threemf';
import { markStale, newId, setState, toast } from '../state/store';
import { documentGeneration, undoable } from '../state/history';
import { beginBusy } from '../state/busy';
import { importing } from '../flow/import';
import { importObjectsDialog } from '../components/Ask';
import { bedArea, newProject } from '../actions';
import { bakeStl, bedBounds, instanceBox, loadProject, nextFreeSpot, outsideBed } from './geometry';
import { listedPlates } from './plates';

type ImportedPlate = { index: number; objects: PlateObject[] };

function placeObjects(existing: PlateObject[], incoming: PlateObject[]): PlateObject[] {
  const area = bedArea();
  const placed = [...existing];
  for (const object of incoming) {
    let next = object;
    const box = instanceBox(object, object.instances[0]);
    const overlaps = placed.some((o) => o.instances.some((i) => {
      const other = instanceBox(o, i);
      return box.min.x < other.max.x - 0.001 && box.max.x > other.min.x + 0.001 &&
        box.min.y < other.max.y - 0.001 && box.max.y > other.min.y + 0.001 &&
        box.min.z < other.max.z - 0.001 && box.max.z > other.min.z + 0.001;
    }));
    if (overlaps || outsideBed([object], area, Infinity).size) {
      const spot = nextFreeSpot(placed, object, area) ?? bedBounds(area).center;
      next = { ...object, instances: object.instances.map((i) => ({ ...i, x: spot.x, y: spot.y })) };
    }
    placed.push(next);
  }
  return placed;
}

export function objectsPatch(state: State, imported: ImportedPlate[]): Partial<State> {
  const slots = Math.max(1, state.filamentSlots.length);
  const plates = listedPlates(state).map((p) => ({ ...p }));
  let nextIndex = Math.max(...plates.map((p) => p.index)) + 1;
  for (const [i, incoming] of imported.entries()) {
    const index = i === 0 ? state.plateIndex : nextIndex++;
    const previous = plates.find((p) => p.index === index);
    const objects = incoming.objects.map((o) => ({ ...o, extruder: Math.max(1, Math.min(slots, o.extruder)) }));
    if (previous) previous.objects = placeObjects(previous.objects, objects);
    else plates.push({ index, name: '', origin: [0, 0], settings: {}, events: [], objects: placeObjects([], objects) });
  }
  const active = plates.find((p) => p.index === state.plateIndex)!;
  const last = active.objects.at(-1);
  return { objects: active.objects, projectPlates: plates.length > 1 || state.projectPlates.length ? plates : [],
    selected: last ? { object: last.id, instance: last.instances[0].id, allInstances: true } : null,
    tab: 'prepare', job: markStale(state.job) };
}

export async function importObjects(file: File, allowNew = false, sourceUrl?: string): Promise<number> {
  const generation = documentGeneration();
  const task = importing.begin('loading');
  const busy = beginBusy(`Reading ${file.name}`, task.signal);
  const plates = new Map<number, ImportedPlate>();
  let items: ProjectItem[] = [], skipped: string[] = [];
  const objects: PlateObject[] = [];
  let committed = false;
  try {
    await loadProject(file, {
      items(list, materials) { items = list; skipped = materials.skipped ?? []; },
      mesh(i, geometry, centre) {
        if (!task.current()) { geometry.dispose(); return; }
        const item = items[i];
        const triangles = (geometry.index?.count ?? geometry.attributes.position.count) / 3;
        const object: PlateObject = { id: newId(), name: item.name, geometry, rotX: 0, rotY: 0, scale: [1, 1, 1],
          extruder: item.extruder, settings: {}, source: new File([], 'object.stl'),
          simplification: { trianglesBefore: triangles, trianglesAfter: triangles, maxErrorMm: 0, kept: 'original' }, ...(sourceUrl ? { sourceUrl } : {}), instances: [{ id: newId(), x: centre.x, y: centre.y, z: Math.max(0, centre.z), rotZ: 0 }] };
        objects.push(object);
        const plate = plates.get(item.plateIndex) ?? { index: item.plateIndex, objects: [] };
        plate.objects.push(object); plates.set(item.plateIndex, plate);
      },
      missing() { skipped.push('objects without meshes'); },
    }, true);
    busy.end();
    if (!task.current()) return 0;
    if (!objects.length) throw new Error(`${file.name}: no objects with meshes`);
    const choice = await importObjectsDialog(file.name, objects.length, [...new Set(skipped)], allowNew);
    if (!choice || !task.current() || generation !== documentGeneration()) return 0;
    const [{ blobDigest }, { assignBlobKey }] = await Promise.all([import('../api/blobs'), import('../persist/session')]);
    await Promise.all(objects.map(async (o) => {
      o.source = new File([await bakeStl(o)], `${o.name.replace(/\.[^.]+$/, '') || 'object'}.stl`, { type: 'model/stl' });
      assignBlobKey(o.source, `sha256-${await blobDigest(o.source)}`);
    }));
    if (!task.current()) return 0;
    if (choice === 'new') {
      task.done();
      if (!await newProject(true)) return 0;
    }
    if (choice !== 'new' && !task.current()) return 0;
    undoable('Import objects', () => setState((state) => objectsPatch(state, [...plates.values()].sort((a, b) => a.index - b.index))));
    committed = true;
    task.done();
    return objects.length;
  } catch (error) {
    if (task.current()) { task.fail(error); toast(error instanceof Error ? error.message : String(error)); }
    return 0;
  } finally {
    busy.end();
    if (!committed) for (const o of objects) o.geometry.dispose();
    if (task.current()) task.done();
  }
}
