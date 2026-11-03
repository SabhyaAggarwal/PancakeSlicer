import { OPTIONS } from '../options';
import { effectiveConfig, getState, type PlateObject, type State } from '../state/store';
import type { Doc } from '../state/history';

export interface VersionChange { key?: string; label: string; from?: string; to?: string }

const shown = (v: unknown) => Array.isArray(v) ? v.join(', ') : String(v ?? '');
const name = (o: PlateObject) => o.name.replace(/\.(stl|obj|3mf|step|stp|drc)$/i, '');

function objectChanges(before: PlateObject[], after: PlateObject[], out: VersionChange[]) {
  const old = new Map(before.map((o) => [o.id, o]));
  for (const o of after) {
    const was = old.get(o.id);
    old.delete(o.id);
    if (!was) { out.push({ key: `o:${o.id}`, label: `Added ${name(o)}` }); continue; }
    if (was === o) continue;
    const what = was.instances !== o.instances ? (was.instances.length !== o.instances.length ? 'Copies of' : 'Moved')
      : was.rotX !== o.rotX || was.rotY !== o.rotY ? 'Rotated' : was.scale !== o.scale ? 'Scaled'
      : was.settings !== o.settings ? 'Settings of' : 'Edited';
    out.push({ key: `o:${o.id}:${what}`, label: `${what} ${name(o)}` });
  }
  for (const o of old.values()) out.push({ key: `o:${o.id}`, label: `Removed ${name(o)}` });
}

export function describeChange(before: Doc, after: Doc): VersionChange[] {
  const out: VersionChange[] = [];
  if (before.objects !== after.objects) objectChanges(before.objects, after.objects, out);
  if (before.projectPlates.length !== after.projectPlates.length)
    out.push({ key: 'plates', label: after.projectPlates.length > before.projectPlates.length ? 'Added plate' : 'Removed plate' });
  if (before.printer !== after.printer) out.push({ key: 'printer', label: 'Printer', from: before.printer, to: after.printer });
  if (before.process !== after.process) out.push({ key: 'process', label: 'Process', from: before.process, to: after.process });
  if (before.filamentSlots !== after.filamentSlots && shown(before.filamentSlots) !== shown(after.filamentSlots)) out.push({ key: 'filaments', label: 'Filaments' });
  if (before.dirty !== after.dirty && before.printer === after.printer && before.process === after.process) {
    const s = getState();
    for (const type of ['process', 'machine'] as const) {
      const a = before.dirty[type] ?? {}, b = after.dirty[type] ?? {};
      if (a === b) continue;
      const keys = [...new Set([...Object.keys(a), ...Object.keys(b)])].filter((k) => shown(a[k]) !== shown(b[k]));
      if (!keys.length) continue;
      const was = effectiveConfig({ ...s, ...before } as State, type), now = effectiveConfig({ ...s, ...after } as State, type);
      for (const k of keys) if (shown(was[k]) !== shown(now[k]))
        out.push({ key: `${type}:${k}`, label: OPTIONS[k]?.label || k, from: shown(was[k]), to: shown(now[k]) });
    }
  }
  if (before.filamentEdits !== after.filamentEdits && !out.some((c) => c.key === 'filaments')) out.push({ key: 'filaments', label: 'Filament settings' });
  return out.slice(0, 8);
}

export const changeText = (c: VersionChange) => c.from !== undefined && c.to !== undefined && (c.from || c.to) ? `${c.label} ${c.from || '–'}→${c.to || '–'}` : c.label;
