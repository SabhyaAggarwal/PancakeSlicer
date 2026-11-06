

import { loadHead } from '../heads';

let scale = { margin: 0.3, floor: 1 };
let loaded = '';

export const clearanceScale = () => scale;


export async function loadClearanceScale(record: Record<string, unknown> | undefined): Promise<boolean> {
  const report = record?.nonplanar_report as { head?: unknown } | undefined;
  const id = typeof report?.head === 'string' ? report.head : '';
  if (!id || id === loaded) return false;
  loaded = id;
  const head = await loadHead(id);
  if (!head) return false;
  const floors = head.boxes.filter((b) => !b.nozzle && b.min[2] > 0).map((b) => b.min[2]);
  scale = { margin: head.margin, floor: floors.length ? Math.min(...floors) : Math.max(1, head.margin) };
  return true;
}
