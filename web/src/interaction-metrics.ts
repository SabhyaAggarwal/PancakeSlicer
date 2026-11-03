const CAPACITY = 128;
type Operation = 'store' | 'history' | 'preview-chunk';
type Region = 'plater' | 'preview' | 'settings' | 'picker' | 'picker-row' | 'manager';
class Samples {
  private values = new Float64Array(CAPACITY);
  private count = 0;
  private total = 0;
  private max = 0;
  add(value: number) {
    if (!Number.isFinite(value) || value < 0) return;
    this.values[this.count++ % CAPACITY] = value;
    this.total += value;
    this.max = Math.max(this.max, value);
  }
  summary() {
    const values = Array.from(this.values.subarray(0, Math.min(this.count, CAPACITY))).sort((a, b) => a - b);
    return { count: this.count, mean: this.count ? this.total / this.count : 0, max: this.max,
      p50: values[Math.floor(values.length * 0.5)] ?? 0, p95: values[Math.floor(values.length * 0.95)] ?? 0,
      percentileSamples: values.length };
  }
}
let enabled = false, started = 0, elapsed = 0, frame = 0, inputFrame = 0;
let timeout: ReturnType<typeof setTimeout> | undefined;
let observer: PerformanceObserver | undefined;
let frames = new Samples(), input = new Samples(), tasks = new Samples();
let operations: Partial<Record<Operation, Samples>> = {};
let renders: Partial<Record<Region, number>> = {};
const events = ['pointerdown', 'pointermove', 'keydown', 'input', 'wheel'] as const;
let lastInput = -Infinity;
const onInput = () => {
  const at = performance.now();
  if (inputFrame || at - lastInput < 50) return;
  lastInput = at;
  inputFrame = requestAnimationFrame(() => { inputFrame = 0; if (enabled) input.add(performance.now() - at); });
};
const stop = () => {
  if (!enabled) return;
  enabled = false;
  elapsed = performance.now() - started;
  clearTimeout(timeout);
  cancelAnimationFrame(frame); cancelAnimationFrame(inputFrame);
  frame = inputFrame = 0;
  observer?.disconnect(); observer = undefined;
  for (const name of events) window.removeEventListener(name, onInput, true);
};
const start = (milliseconds = 10_000) => {
  stop();
  frames = new Samples(); input = new Samples(); tasks = new Samples(); operations = {}; renders = {};
  started = performance.now(); elapsed = 0; lastInput = -Infinity; enabled = true;
  let previous = 0;
  const tick = (at: number) => { if (!enabled) return; if (previous) frames.add(at - previous); previous = at; frame = requestAnimationFrame(tick); };
  frame = requestAnimationFrame(tick);
  for (const name of events) window.addEventListener(name, onInput, { capture: true, passive: true });
  try { observer = new PerformanceObserver((list) => { if (enabled) for (const task of list.getEntries()) tasks.add(task.duration); }); observer.observe({ type: 'longtask' }); } catch { observer = undefined; }
  const duration = Number.isFinite(milliseconds) ? Math.min(30_000, Math.max(1000, milliseconds)) : 10_000;
  timeout = setTimeout(stop, duration);
};
const noop = () => {};
export function timeInteraction(name: Operation): () => void {
  if (!enabled) return noop;
  const at = performance.now(), session = started;
  return () => { if (enabled && started === session) (operations[name] ??= new Samples()).add(performance.now() - at); };
}
const regions = new Set<Region>(['plater', 'preview', 'settings', 'picker', 'picker-row', 'manager']);
export function countRender(name: Region) { if (enabled && regions.has(name)) renders[name] = (renders[name] ?? 0) + 1; }
const snapshot = () => {
  const duration = enabled ? performance.now() - started : elapsed;
  return { recording: enabled, elapsedMs: duration, frames: frames.summary(), inputToFrame: input.summary(), longTasks: tasks.summary(),
    operations: Object.fromEntries(Object.entries(operations).map(([key, value]) => [key, value.summary()])), renders: { ...renders },
    note: 'CPU input-to-rAF proxy; percentiles use at most 128 samples. No compositor/input-latency guarantee.' };
};
export const interactionMetrics = { start, stop, snapshot, countRender };
declare global { interface Window { orcaPerf: typeof interactionMetrics } }
if (typeof window !== 'undefined') window.orcaPerf = interactionMetrics;
