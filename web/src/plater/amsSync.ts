import type { SystemPreset } from '../api/types';
import { readTrays, type Tray } from '../printers/connections';
import { compatible, getState, markStale, setState, toast } from '../state/store';
import { undoable } from '../state/history';

const EXTERNAL = 254;

function presetFor(t: Tray, fils: SystemPreset[]): SystemPreset | undefined {
  const byId = t.filament_id && fils.find((f) => f.filament_id === t.filament_id);
  if (byId) return byId;
  const named = t.name && fils.find((f) => f.name.startsWith(`Bambu ${t.name} @`));
  if (named) return named;
  const generic = fils.filter((f) => f.name.startsWith('Generic '));
  const type = t.type.toUpperCase();
  return generic.find((f) => f.name.toUpperCase().startsWith(`GENERIC ${type}`)) ??
    generic.map((f) => ({ f, ft: String(f.config.filament_type ?? '').toUpperCase() }))
      .filter(({ ft }) => ft && new RegExp(`(^|[^A-Z0-9])${ft.replace(/[^A-Z0-9]/g, '\\$&')}($|[^A-Z0-9])`).test(type))
      .sort((a, b) => b.ft.length - a.ft.length)[0]?.f;
}

export async function syncAms(printer: string) {
  let trays: Tray[];
  try {
    trays = await readTrays(printer);
  } catch (e) {
    toast(`Sync filaments with AMS: ${e instanceof Error ? e.message : String(e)}`);
    return;
  }
  const loaded = trays.filter((t) => t.type && t.id !== EXTERNAL);
  const used = loaded.length ? loaded : trays.filter((t) => t.type && t.id === EXTERNAL);
  if (!used.length) {
    toast('Sync filaments with AMS: the printer reports no loaded filament.');
    return;
  }
  const s = getState(), fils = compatible(s, 'filament');
  const picked = used.map((t) => ({ t, preset: presetFor(t, fils) }));
  const unknown = picked.filter((p) => !p.preset);
  if (unknown.length === picked.length) {
    toast('There are no compatible filaments, and sync is not performed.');
    return;
  }
  const fallback = fils.find((f) => f.name.startsWith('Generic PLA')) ?? picked.find((p) => p.preset)!.preset!;
  const slots = picked.map((p) => (p.preset ?? fallback).name);
  const n = slots.length;
  undoable('Sync filaments with AMS', () => setState((st) => ({
    filamentSlots: slots,
    slotColours: picked.map((p) => p.t.color || undefined),
    filamentEdits: Object.fromEntries(Object.entries(st.filamentEdits).filter(([name]) => slots.includes(name))),
    flush: { ...st.flush, matrix: null }, mixes: {},
    objects: st.objects.map((o) => (o.extruder > n ? { ...o, extruder: 1 } : o)),
    events: st.events.filter((e) => e.type !== 'change' || Number(e.text) <= n),
    job: markStale(st.job),
  })));
  if (unknown.length)
    toast(`There are some unknown or incompatible filaments mapped to generic preset:${unknown.map((p) => `\n- ${p.t.name || 'AMS'} (${p.t.type})`).join('')}`);
}
