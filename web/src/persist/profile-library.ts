import { mutation, responseError } from '../api/access';
import type { Config, ProfileOverlay } from '../api/types';
import type { ProfileLibraryDocument, ProfileLibraryWrite, ProfileLibraryWriteResult } from '../profiles/ownership';
import type { SpeedModel } from '../profiles/speed-rules';
import type { ScaleRow } from '../profiles/auto-rules';
import { filamentCatalog, printerCatalog } from '../profiles/catalog-reads';

type Type = 'printer' | 'filament' | 'print';
interface V4Record {
  id: string; type: Type; name: string; revision: number; archived?: { at: number; operationId: string };
  vendor?: string; model?: string; nozzle?: number; machine?: Config; printOverrides?: Config;
  family?: string; brand?: string; material?: string; colour?: string; settings?: Config; fits?: { models?: string[]; nozzles?: number[] };
  values?: Config; scales?: ScaleRow[]; preferredHeight?: number; speedModel?: SpeedModel;
}
interface V4Document { schema: 4; revision: number; epoch?: string; profiles: V4Record[]; setups: ProfileLibraryDocument['printers']['setups'] }

const AUTO = { name: 'Auto', engineName: 'Auto', version: 'auto:1' };
const v3Type = { printer: 'machine', filament: 'filament', print: 'process' } as const;
const v4Type = { machine: 'printer', filament: 'filament', process: 'print' } as const;
const first = (v: Config[string] | undefined) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');

const nozzleKey = (n: number) => String(n);
const writtenFilaments = new Map<string, { family: string; base: ProfileOverlay['base']; metadata?: Config }>();
const machineName = async (vendor: string, model: string, nozzle: number) =>
  (await printerCatalog()).find((v) => v.id === vendor)?.models.find((m) => m.id === model)?.machines?.[nozzleKey(nozzle)];

async function fromV4(doc: V4Document): Promise<ProfileLibraryDocument> {
  for (const r of doc.profiles) if (r.type === 'filament') void filamentCatalog((r.family ?? '').split('/')[0]).catch(() => []);
  const printers = doc.profiles.filter((p) => p.type === 'printer');
  const machines = new Map(await Promise.all(printers.map(async (p) => [p.id, await machineName(p.vendor ?? '', p.model ?? '', p.nozzle ?? 0)] as const)));
  const home = printers.find((p) => !p.archived && machines.get(p.id)) ?? printers.find((p) => machines.get(p.id));
  const profiles = await Promise.all(doc.profiles.map(async (r): Promise<ProfileOverlay> => {
    const common = { id: r.id, name: r.name, revision: r.revision, kind: 'profile' as const, type: v3Type[r.type], ...(r.archived ? { archived: r.archived } : {}) };
    if (r.type === 'printer') {
      const name = machines.get(r.id) ?? `${r.model} ${r.nozzle} nozzle`;
      return { ...common, base: { name, engineName: name, version: '', vendor: r.vendor }, overlay: { ...r.machine },
        ...(r.printOverrides ? { printOverrides: { ...r.printOverrides } } : {}) } as ProfileOverlay;
    }
    if (r.type === 'filament') {
      const vendor = (r.family ?? '').split('/')[0], family = (await filamentCatalog(vendor).catch(() => [])).find((f) => f.id === r.family);
      const written = writtenFilaments.get(r.id)?.family === r.family ? writtenFilaments.get(r.id) : undefined;
      const name = written?.base.name ?? (home && family?.variants?.[machines.get(home.id)!]) ?? family?.fallback ?? Object.values(family?.variants ?? {})[0] ?? r.family ?? '';
      const overlay = { ...r.settings, ...(r.colour ? { filament_colour: [r.colour] } : {}) };
      const applicability = r.fits ? { schema: 2, ...(r.fits.models?.length ? { printers: { models: r.fits.models } } : {}), ...(r.fits.nozzles?.length ? { nozzle: { sizes: r.fits.nozzles } } : {}) } : undefined;
      return { ...common, base: written?.base ?? { name, engineName: name, version: '', vendor }, overlay, ...(applicability ? { applicability } : {}),
        ...(written ? (written.metadata ? { metadata: written.metadata } : {}) : { metadata: { family: r.family ?? '' } }) } as ProfileOverlay;
    }
    const auto = r.scales?.length || r.preferredHeight || r.speedModel ? { auto: { schema: 3, scales: r.scales ?? [], ...(r.speedModel ? { speedModel: r.speedModel } : {}), ...(r.preferredHeight ? { preferredHeight: r.preferredHeight } : {}) } } : {};
    return { ...common, base: AUTO, overlay: { ...r.values }, ...auto } as ProfileOverlay;
  }));
  return { schema: 1, revision: doc.revision, ...(doc.epoch ? { epoch: doc.epoch } : {}), profiles, printers: { schema: 1, revision: 0, setups: doc.setups ?? {} } };
}

async function toV4(p: ProfileOverlay): Promise<V4Record> {
  const type = v4Type[p.type], extra = p as ProfileOverlay & { printOverrides?: Config; auto?: { scales?: ScaleRow[]; preferredHeight?: number; speedModel?: SpeedModel } };
  const base: V4Record = { id: p.id!, type, name: p.name.replace(/\s+@.*$/, '').trim() || p.name, revision: p.revision ?? 0, ...(p.archived ? { archived: p.archived } : {}) };
  const overlay = { ...p.overlay };
  if (type === 'printer') {
    const name = p.base.name, catalog = await printerCatalog();
    for (const v of catalog) for (const m of v.models) for (const [nozzle, machine] of Object.entries(m.machines ?? {}))
      if (machine === name) return { ...base, vendor: v.id, model: m.id, nozzle: Number(nozzle), ...(Object.keys(overlay).length ? { machine: overlay } : {}),
        ...(extra.printOverrides && Object.keys(extra.printOverrides).length ? { printOverrides: extra.printOverrides } : {}) };
    const model = first(overlay.printer_model), nozzle = Number(first(overlay.nozzle_diameter));
    return { ...base, vendor: p.base.vendor ?? '', model, nozzle, ...(Object.keys(overlay).length ? { machine: overlay } : {}) };
  }
  if (type === 'filament') {
    const vendor = p.base.vendor ?? 'BBL', list = await filamentCatalog(vendor).catch(() => []);
    const family = String(p.metadata?.family ?? '') || list.find((f) => f.fallback === p.base.name || Object.values(f.variants ?? {}).includes(p.base.name))?.id || `${vendor}/${p.base.name.replace(/\s+@.*$/, '')}`;
    writtenFilaments.set(p.id!, { family, base: p.base, ...(p.metadata ? { metadata: p.metadata } : {}) });
    const known = list.find((f) => f.id === family), colour = first(overlay.filament_colour);
    delete overlay.filament_colour;
    const rule = (p as { applicability?: { printers?: { models?: string[] }; nozzle?: { sizes?: number[] } } }).applicability;
    const fits = rule?.printers?.models?.length || rule?.nozzle?.sizes?.length ? { ...(rule.printers?.models?.length ? { models: rule.printers.models } : {}), ...(rule.nozzle?.sizes?.length ? { nozzles: rule.nozzle.sizes } : {}) } : undefined;
    return { ...base, family, ...(known?.brand ? { brand: known.brand } : {}), ...(known?.material ? { material: known.material } : {}), ...(colour ? { colour } : {}),
      ...(Object.keys(overlay).length ? { settings: overlay } : {}), ...(fits ? { fits } : {}) };
  }
  return { ...base, ...(Object.keys(overlay).length ? { values: overlay } : {}), ...(extra.auto?.scales?.length ? { scales: extra.auto.scales } : {}),
    ...(extra.auto?.speedModel ? { speedModel: extra.auto.speedModel } : {}),
    ...(extra.auto?.preferredHeight ? { preferredHeight: extra.auto.preferredHeight } : {}) };
}

export async function readProfileLibrary(_mock = false): Promise<ProfileLibraryDocument> {
  const response = await fetch('/api/library');
  if (!response.ok) throw await responseError(response);
  const doc = await response.json() as V4Document;
  if (!doc || doc.schema !== 4 || !Number.isInteger(doc.revision) || !Array.isArray(doc.profiles)) throw new Error('The profile library is invalid.');
  return fromV4(doc);
}
export async function persistProfileLibrary(write: ProfileLibraryWrite, _mock = false): Promise<ProfileLibraryWriteResult> {
  const { document } = write;
  const v4: V4Document = { schema: 4, revision: document.revision, ...(document.epoch ? { epoch: document.epoch } : {}),
    profiles: await Promise.all(document.profiles.filter((p) => p.id).map(toV4)), setups: document.printers.setups };
  const response = await mutation('/api/library', { method: 'PUT', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ operationId: write.operationId, expectedRevision: write.expectedRevision, document: v4 }) });
  if (!response.ok) throw await responseError(response);
  return response.json();
}
