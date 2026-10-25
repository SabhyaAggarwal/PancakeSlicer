import { getState, printerPreset, setState, subscribeStore, type OrientPrompt, type PlateObject, type State } from '../state/store';
import { undoable } from '../state/history';
import { instanceBox, parsePoints } from './geometry';
import { orientJob } from './operations';
import { rotationToZ, withRotation } from './rotation';

interface Score { placement?: unknown; up?: unknown; support_volume?: unknown; float_area?: unknown; time_s?: unknown; stable?: unknown }

const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : undefined);
const pose = (o: PlateObject) => JSON.stringify([o.rotX, o.rotY, o.scale]);

function fits(o: PlateObject, rotation: number[]) {
  const machine = printerPreset(getState());
  if (!machine) return false;
  const area = parsePoints(machine.config.printable_area), xs = area.map((p) => p.x), ys = area.map((p) => p.y);
  const turned = withRotation(o, rotation);
  const box = instanceBox(turned, turned.instances[0]);
  return box.max.x - box.min.x <= Math.max(...xs) - Math.min(...xs) && box.max.y - box.min.y <= Math.max(...ys) - Math.min(...ys) &&
    box.max.z - box.min.z <= Number(machine.config.printable_height ?? 1e6) + 1e-6;
}

function decide(o: PlateObject, scores: Score[]): OrientPrompt | null {
  const original = scores.find((x) => x.placement === 'original');
  const support = num(original?.support_volume), floating = num(original?.float_area), time = num(original?.time_s);
  if (support === undefined || floating === undefined || support <= 0) return null;
  for (const c of scores) {
    const up = c.up as number[];
    const s = num(c.support_volume), f = num(c.float_area), t = num(c.time_s);
    if (c.placement !== 'candidate' || s === undefined || f === undefined || c.stable === 0 || s > 0.5 * support || f > floating) continue;
    if (!Array.isArray(up) || up.length !== 3 || !up.every(Number.isFinite) || up[2] > Math.cos(2 * Math.PI / 180)) continue;
    const rotation = rotationToZ(up as [number, number, number]);
    if (!fits(o, rotation)) continue;
    return { rotation, faster: time && t !== undefined && t < time ? Math.round((100 * (time - t)) / time) : 0, pose: pose(o) };
  }
  return null;
}

const without = (st: State, id: number) => {
  const next = { ...st.orientPrompts };
  delete next[id];
  return next;
};

const scored = new Map<number, string>();
let queue = Promise.resolve();

async function score(o: PlateObject, printer: string) {
  let scores: Score[] = [];
  try {
    const record = await orientJob(o, 'orient-scores');
    scores = Array.isArray(record?.scores) ? (record.scores as Score[]) : [];
  } catch {
    return;
  }
  const s = getState(), now = s.objects.find((x) => x.id === o.id);
  if (!now || s.printer !== printer || pose(now) !== pose(o) || s.orientDismissed.includes(o.id)) return;
  const prompt = decide(now, scores);
  setState((st) => ({ orientPrompts: prompt ? { ...st.orientPrompts, [o.id]: prompt } : without(st, o.id) }));
}

function check() {
  const s = getState();
  if (!s.features.orient_scores || !s.api || s.api.mock || s.project) return;
  for (const o of s.objects) {
    const key = `${s.printer}|${pose(o)}`;
    if (o.project?.placeholder || s.orientDismissed.includes(o.id) || scored.get(o.id) === key) continue;
    scored.set(o.id, key);
    if (s.orientPrompts[o.id] && s.orientPrompts[o.id].pose !== pose(o)) setState((st) => ({ orientPrompts: without(st, o.id) }));
    const printer = s.printer;
    queue = queue.then(() => score(o, printer));
  }
}

let started = false;
export function startOrientPrompt() {
  if (started) return;
  started = true;
  subscribeStore(check);
  check();
}

export function applyOrientPrompt(id: number) {
  const s = getState(), prompt = s.orientPrompts[id], o = s.objects.find((x) => x.id === id);
  if (!prompt || !o || pose(o) !== prompt.pose) {
    setState((st) => ({ orientPrompts: without(st, id) }));
    return;
  }
  const turned = withRotation(o, prompt.rotation);
  undoable('Orient', () => setState((st) => ({ objects: st.objects.map((x) => (x.id === id ? turned : x)), orientPrompts: without(st, id) })));
}
