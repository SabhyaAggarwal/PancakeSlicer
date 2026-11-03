export type NoticeKind = 'error' | 'warning' | 'success' | 'info';
export interface NoticeEntry { id: number; message: string; kind: NoticeKind }

const ERROR = /\b(fail(ed|s)?|could not|cannot|can't|unable|not supported|requires|is locked|is not a model|are supported|no (model|printable|compatible|more)|does not fit|do not fit|is full|paused|best effort|open edges|unknown or incompatible)\b/i;
const SUCCESS = /\b(saved|exported|created|imported|repaired)\b|successfully/i;

export function noticeKind(msg: string): NoticeKind {
  if (ERROR.test(msg)) return 'error';
  return SUCCESS.test(msg) && !/…$/.test(msg) ? 'success' : 'info';
}

export const shouldToast = (msg: string, action?: { label: string } | null, kind?: NoticeKind) =>
  (kind ? kind === 'error' || kind === 'warning' : noticeKind(msg) === 'error') || action?.label === 'Undo';

export const noticeMs = (msg: string) => (noticeKind(msg) === 'error' ? 8000 : 3500);

const TOAST_STEPS: [RegExp, string][] = [
  [/^Presets failed to load/i, 'Reload the page to try again.'],
  [/^Export failed/i, 'Slice the plate again, then export.'],
  [/^Save failed|^Saving the preset failed|^Deleting the preset failed/i, 'Check the connection and try again.'],
  [/^Sample download failed/i, 'Check the connection and try again.'],
  [/requires the slicing service/i, 'Demo mode cannot save projects.'],
  [/^The plate is locked/i, 'Unlock it with the lock beside the plate.'],
  [/does not fit|do not fit|is full|No more copies/i, 'Scale down, remove copies or add a plate.'],
  [/^Arrange failed/i, 'Try Arrange again (A).'],
  [/Cannot read the archive|holds no model/i, 'Choose another file.'],
];

export function toastStep(msg: string): string | null {
  return TOAST_STEPS.find(([re]) => re.test(msg))?.[1] ?? null;
}

const SLICE_CAUSES: [RegExp, string, string][] = [
  [/negative spacing|extrusion width|line width/i, 'A line width is too small', 'Raise it under Process › Quality › Line width.'],
  [/empty layer|no layers|nothing to (print|slice)|no extrusion|zero-volume|too small/i, 'Nothing on the plate prints', 'Check the model sits on the bed and is not tiny, then slice again.'],
  [/outside|print volume|exceeds|printable area/i, 'An object is off the bed', 'Arrange (A) or move it inside the bed, then slice again.'],
  [/timed? ?out|deadline/i, 'The slice took too long', 'Simplify the model or slice fewer objects.'],
  [/failed to fetch|networkerror|network|connection|disconnected/i, 'The connection dropped', 'Check your connection, then slice again.'],
  [/plate-index|upgrade/i, 'The slicing service is out of date', 'Ask your administrator to update it.'],
  [/non-?manifold|open edges|self-?intersect|mesh/i, 'The model has mesh errors', 'Right-click it, Fix Model, then slice again.'],
  [/memory|bad_alloc|killed|signal 9/i, 'The slicer ran out of memory', 'Slice fewer or smaller objects.'],
];

export function sliceFailure(raw: string) {
  const hit = SLICE_CAUSES.find(([re]) => re.test(raw));
  return { title: hit?.[1] ?? 'The slice failed', step: hit?.[2] ?? 'Check the settings it names, then slice again.' };
}
