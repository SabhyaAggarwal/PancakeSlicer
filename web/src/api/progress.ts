import type { ProgressEvent } from './types';

const STATUS: Record<string, string> = {
  uploading: 'Uploading', queued: 'Queued', storing: 'Saving the result', load: 'Loading model', loading: 'Loading model',
  perimeters: 'Generating perimeters', infill: 'Infilling layers', support: 'Generating support material',
  skirt: 'Generating skirt & brim', export: 'Exporting G-code', gcode: 'Exporting G-code',
  verify: 'Checking toolpath clearance', json: 'Writing slice record', package: 'Packaging sliced plate',
  nonplanar: 'Slicing curved layers', curved: 'Slicing curved layers',
  rotary: 'Slicing rotary curved layers', multiplanar: 'Slicing multiplanar chunks',
  shells: 'Generating curved top shells', atom: 'Importing the Atomizer toolpath', contour: 'Z contouring',
};

export function sliceProgress(e: ProgressEvent): ProgressEvent {
  if (e.stage === 'queued' && e.queuePosition) return { stage: 'queued', pct: 0, queuePosition: e.queuePosition };
  const pct = Math.max(0, Math.min(100, e.pct));
  let text = STATUS[e.stage] ?? e.stage.replace(/_/g, ' ');
  if (e.stage === 'slice' || e.stage === 'slicing') {
    text = pct >= 100 ? 'Slicing complete' : pct >= 75 ? 'Optimizing toolpath' : pct >= 71 ? 'Detect overhangs for auto-lift'
      : pct >= 70 ? 'Generating skirt & brim' : pct >= 50 ? 'Generating support material'
      : pct >= 40 ? 'Slicing' : pct >= 35 ? 'Infilling layers' : pct >= 25 ? 'Generating infill regions'
      : pct >= 15 ? 'Generating perimeters' : 'Slicing mesh';
  }
  const ranges: Record<string, [number, number]> = {
    load: [0, 5], slice: [5, 75], slicing: [5, 75], export: [80, 12],
    verify: [92, 3], json: [97, 2], package: [95, 2],
  };
  const range = ranges[e.stage];
  return { stage: e.message || text || 'Slicing', pct: Math.min(99, range ? range[0] + pct * range[1] / 100 : pct) };
}

export function slicingText(job: { stage: string; pct: number; queuePosition?: number }, plateIndex = 1): string {
  if (job.stage === 'queued' && job.queuePosition)
    return `Slicing plate ${plateIndex}: Queued · ${job.queuePosition > 1 ? `${job.queuePosition - 1} ahead` : 'next'}`;
  return `Slicing plate ${plateIndex}: ${STATUS[job.stage] ?? (job.stage || 'Slicing')} ${Math.round(job.pct)}%`;
}
