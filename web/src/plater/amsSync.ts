import { readPrinterSlots, type SlotMatch } from '../printers/slots';
import { getState, markStale, resolvePreset, setState, toast } from '../state/store';
import { undo, undoable } from '../state/history';
import { filamentCatalog } from '../profiles/catalog-reads';
import { addCatalogFamily } from '../profiles/catalog-add';

export async function syncAms(printer: string) {
  let slots: SlotMatch[];
  try {
    slots = (await readPrinterSlots(printer)).slots;
  } catch (e) {
    toast(`Match printer filaments: ${e instanceof Error ? e.message : String(e)}`);
    return;
  }
  const loaded = slots.filter((m) => !m.slot.startsWith('ext'));
  const used = loaded.length ? loaded : slots.filter((m) => m.slot.startsWith('ext'));
  if (!used.length) {
    toast('Your printer says no filaments are currently loaded.');
    return;
  }
  const added: string[] = [];
  const keys: string[] = [];
  for (const m of used) {
    if (m.filament) { keys.push(m.filament); continue; }
    const vendor = resolvePreset(getState(), 'machine', printer).base?.vendor ?? '';
    const family = (await filamentCatalog(vendor)).find((f) => Object.values(f.variants).includes(m.catalog ?? ''));
    if (!family) {
      toast(`${m.label} has ${m.tray.type} loaded, which doesn't have a matching ${m.name} profile for this printer yet.`, null, null, 'warning');
      return;
    }
    keys.push(await addCatalogFamily(family, m.tray.colour || undefined));
    added.push(m.name);
  }
  const n = keys.length;
  undoable('Set project filaments', () => setState((st) => ({
    filamentSlots: keys,
    slotColours: used.map((m) => m.tray.colour || undefined),
    filamentEdits: Object.fromEntries(Object.entries(st.filamentEdits).filter(([name]) => keys.includes(name))),
    flush: { ...st.flush, matrix: null }, mixes: {},
    objects: st.objects.map((o) => (o.extruder > n ? { ...o, extruder: 1 } : o)),
    events: st.events.filter((e) => e.type !== 'change' || Number(e.text) <= n),
    job: markStale(st.job),
  })));
  toast(`Synced ${n} filament${n === 1 ? '' : 's'} from your printer${added.length ? `; added ${added.join(', ')} to My filaments` : ''}!`, { label: 'Undo', run: undo }, 8000, 'success');
}
