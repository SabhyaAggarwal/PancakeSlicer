export type NoticeKind = 'error' | 'warning' | 'success' | 'info';
export interface NoticeEntry { id: number; message: string; kind: NoticeKind }

const ERROR = /\b(fail(ed|s)?|could not|couldn't|cannot|can't|unable|not supported|isn't supported|isn't available|requires?|is locked|is not a model|isn't a supported|are supported|no (model|printable|compatible|more)|does not fit|do not fit|is full|paused|best[ -]effort|open edges|unknown or incompatible|oops|didn't (finish|accept|respond)|doesn't (have enough|support|define)|is softer)\b/i;
const SUCCESS = /\b(saved|exported|created|imported|repaired|added)\b|successfully/i;

export function noticeKind(msg: string): NoticeKind {
  if (ERROR.test(msg)) return 'error';
  return SUCCESS.test(msg) && !/…$/.test(msg) ? 'success' : 'info';
}

export const shouldToast = (msg: string, action?: { label: string } | null, kind?: NoticeKind) =>
  (kind ? kind === 'error' || kind === 'warning' : noticeKind(msg) === 'error') || action?.label === 'Undo';

export const noticeMs = (msg: string) => (noticeKind(msg) === 'error' ? 8000 : 3500);

const TOAST_STEPS: [RegExp, string][] = [
  [/^Presets failed to load/i, 'Reload the page and give it another try.'],
  [/^(Oops! )?Export failed/i, 'Slice the plate again, then export your file.'],
  [/^Oops! Couldn't save: |^Saving the preset failed|^Deleting the preset failed/i, 'Check your network connection and try again.'],
  [/^Sample download failed/i, 'Check your network connection and try again.'],
  [/requires a connection to the slicing service/i, 'Saving projects to the server isn\'t available in demo mode.'],
  [/^This plate is locked/i, 'Click the lock icon next to the plate to unlock it first.'],
  [/does not fit|do not fit|plate is full|No more copies/i, 'Try scaling the model down, removing a few copies, or adding another plate.'],
  [/^Oops! Couldn't auto-arrange/i, 'Try clicking Arrange (A) again.'],
  [/Couldn't open that archive|holds no model/i, 'Please try a different file.'],
];

export function toastStep(msg: string): string | null {
  return TOAST_STEPS.find(([re]) => re.test(msg))?.[1] ?? null;
}

const SLICE_CAUSES: [RegExp, string, string][] = [
  [/negative spacing|extrusion width|line width/i, 'Oops! One of the line widths is too thin', 'Try increasing it under Process › Quality › Line width.'],
  [/empty layer|no layers|nothing to (print|slice)|no extrusion|zero-volume|too small/i, 'Oops! Nothing on the plate produced any printable layers', 'Make sure your model is sitting flat on the bed and isn\'t microscopic, then slice again.'],
  [/outside|print volume|exceeds|printable (bed )?area|past the edge/i, 'Oops! Part of a model is outside the build plate', 'Press Arrange (A) or drag the model back inside the bed boundaries, then slice again.'],
  [/timed? ?out|deadline/i, 'Oops! Slicing took too long and timed out', 'Try simplifying the mesh or slicing fewer objects at once.'],
  [/failed to fetch|networkerror|network|connection|disconnected/i, 'Oops! Connection lost', 'Check your connection and try slicing again.'],
  [/plate-index|upgrade/i, 'The slicing service needs an update', 'Please update your slicing service (or ask your server admin) and try again.'],
  [/non-?manifold|open edges|self-?intersect|mesh|non-finite|no vertices/i, 'Oops! This model has some broken mesh geometry', 'Right-click the model and choose Fix Model, then try slicing again.'],
  [/memory|bad_alloc|killed|signal 9/i, 'Oops! The slicer ran out of memory', 'Try slicing fewer objects at once or simplifying complex meshes.'],
];

export function sliceFailure(raw: string) {
  const hit = SLICE_CAUSES.find(([re]) => re.test(raw));
  return { title: hit?.[1] ?? 'Oops! Slicing didn\'t finish', step: hit?.[2] ?? 'Take a look at the highlighted settings above, tweak them, and give slicing another go!' };
}
