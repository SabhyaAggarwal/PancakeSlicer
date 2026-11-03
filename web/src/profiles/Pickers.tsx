import { modelLabel, nozzleField, printerCover, profileColour, rowDetail, vendorField, vendorLabel } from './metadata';
import type { Config, PresetType } from '../api/types';
import { getState, resolvePreset, setState, useStore } from '../state/store';
import { Picker, type PickerAction, type PickerGroup, type PickerItem } from '../components/picker/Picker';
import { requestPreset } from '../components/PresetCombo';
import { filamentColor } from '../plater/filamentColor';
import { addSlot } from './SetupRows';
import { FilamentColourMenu } from './FilamentColourMenu';
import { createProfileFromBase, listCatalog, listProfiles, type PickerEntry } from './pickerSource';
import { pickerStatus } from '../printers/connections';
import { profileKey } from './identity';
import { worksWithNone } from './WorksWith';

const first = (v: unknown) => String([v].flat()[0] ?? '');
const shortName = (name: string) => name.replace(/\s*@.*$/, '') || name;
export const presets = (type: PresetType, name?: string): PickerAction => ({ label: `Manage ${type === 'machine' ? 'printers' : type === 'filament' ? 'filaments' : 'print settings'}`, run: () => setState({ presetUx: { kind: 'presets', type, name } }) });

export function noPrinterGroup(type: 'filament' | 'process'): PickerGroup[] {
  const items = getState().userPresets.filter((p) => p.type === type && !p.archived && (p.kind ?? 'profile') === 'profile' && worksWithNone(type, p.applicability))
    .map((p): PickerItem => ({ id: profileKey(p), label: p.name, search: p.name })).sort((a, b) => a.label.localeCompare(b.label));
  return items.length ? [{ id: 'none', title: 'No printer', items, allOnly: true }] : [];
}
export function pickOrOpen(type: 'filament' | 'process', id: string, pick: (id: string) => unknown) {
  if (noPrinterGroup(type)[0]?.items.some((i) => i.id === id)) setState({ presetUx: { kind: 'presets', type, name: id } });
  else pick(id);
}

function brandName(name: string, c: Config) {
  const brand = first(c.filament_vendor) || 'Generic', short = shortName(name);
  const word = brand.split(/\s+/)[0];
  return { brand, name: short.toLowerCase().startsWith(word.toLowerCase() + ' ') ? short.slice(word.length + 1) : short };
}

const MATERIALS = ['PLA', 'PETG', 'ABS', 'ASA', 'TPU', 'PC', 'PA', 'PVA'];
const RARE = /^(PVA|BVOH|HIPS|EVA|COPE|PVB|SUPPORT|OTHER)$/;
export const materialRank = (t: string) => { const i = MATERIALS.indexOf(t.toUpperCase()); return i >= 0 ? i : RARE.test(t.toUpperCase()) ? 99 : 50; };
const material = (type: string) => (type.toUpperCase().split(/[-+\s]/)[0] || 'Other').replace(/^PA\d+$/, 'PA');

export function currentEntry(type: PresetType, key: string | undefined, entries: readonly PickerEntry[]): PickerEntry | undefined {
  if (!key || entries.some((e) => e.key === key)) return undefined;
  const r = resolvePreset(getState(), type, key);
  return r.name ? { key, name: r.name, config: r.config, vendor: r.base?.vendor ?? '', mine: true } : undefined;
}
export const NOT_COMPATIBLE = 'Not compatible';

export function usePickerPending(resolved: boolean): boolean {
  const load = useStore((s) => s.profileLibraryLoad.state), api = useStore((s) => !!s.api);
  return !api || load === 'loading' || load === 'failed' && !resolved;
}
export const PickerSkeleton = () => <><span className="pk-name" aria-label="Loading"><span className="skel-line pk-skel-name"/></span><span className="pk-sub"><span className="skel-line pk-skel-sub"/></span></>;

const listCache = new Map<string, { at: unknown; value: PickerGroup[] }>();
export const sameStateGroups = (key: string, build: () => PickerGroup[]) => (): PickerGroup[] => {
  const at = getState(), hit = listCache.get(key);
  if (hit && hit.at === at) return hit.value;
  const value = build(); listCache.set(key, { at, value }); return value;
};

function filamentGroups(slot: number | null, catalog = false): PickerGroup[] {
  const s = getState();
  let entries = catalog ? listCatalog('filament') : listProfiles('filament');
  const hidden = catalog || slot === null ? undefined : currentEntry('filament', s.filamentSlots[slot], entries);
  if (hidden) entries = [...entries, hidden];
  const others = new Map<string, { n: number; colour: string }[]>();
  if (!catalog) s.filamentSlots.forEach((name, i) => { if (i !== slot && name) others.set(name, [...(others.get(name) ?? []), { n: i + 1, colour: filamentColor(s, i + 1) }]); });
  const item = (e: PickerEntry): PickerItem => {
    const brand = first(e.config.filament_vendor) || e.vendor || 'Generic', b = brandName(e.name, e.config);
    const label = e.mine ? e.name : b.name;
    return { id: e.key, label, sub: rowDetail(label, [vendorField(brand)]) || undefined, facet: brand, swatch: profileColour(e.config), disabled: e.reason ?? false, search: `${brand} ${first(e.config.filament_type)} ${e.name}`, slots: others.get(e.key) };
  };
  const seen = new Map<string, number>();
  for (const e of entries) { const k = item(e).label + first(e.config.filament_vendor); seen.set(k, (seen.get(k) ?? 0) + 1); }
  const types = new Map<string, PickerItem[]>(), loaded: PickerItem[] = [];
  for (const e of entries) {
    const it = item(e), at = e.name.match(/@\s*(.+)$/)?.[1];
    if (e === hidden) it.meta = NOT_COMPATIBLE;
    if (!e.mine && at && !/system/i.test(at) && seen.get(it.label + first(e.config.filament_vendor))! > 1) it.label += ` · ${at.replace(/^BBL\s+/, '')}`;
    const type = material(first(e.config.filament_type));
    types.set(type, [...(types.get(type) ?? []), it]);
    if (it.slots) loaded.push(it);
  }
  const rank = materialRank;
  const groups: PickerGroup[] = [];
  if (loaded.length > 1) groups.push({ id: 'loaded', title: 'Loaded', items: [...loaded].sort((a, b) => a.slots![0].n - b.slots![0].n), filter: true });
  for (const type of [...types.keys()].sort((a, b) => rank(a) - rank(b) || a.localeCompare(b)))
    groups.push({ id: `t:${type}`, title: type, items: types.get(type)!.sort((a, b) => a.facet!.localeCompare(b.facet!) || a.label.localeCompare(b.label)) });
  return catalog ? groups : [...groups, ...noPrinterGroup('filament')];
}

export function FilamentPicker({ slot }: { slot: number }) {
  const name = useStore((s) => s.filamentSlots[slot] ?? '');
  useStore((s) => s.filamentEdits);
  useStore((s) => s.userPresets);
  const s = getState();
  const resolved = resolvePreset(s, 'filament', name), c = resolved.config, label = resolved.name ?? 'Unavailable filament';
  const b = brandName(label, c), pending = usePickerPending(!!resolved.name);
  return (
    <span className="pk-slot">
      <FilamentColourMenu slot={slot}/>
      <Picker label={`Filament ${slot + 1}`} value={name} sections memory="orca.web.picker.filament" facetHeads groups={sameStateGroups(`filament:${slot}`, () => filamentGroups(slot))}
        catalog={{ label: 'Add filament', groups: sameStateGroups(`filament-catalog:${slot}`, () => filamentGroups(slot, true)), onAdd: (id) => createProfileFromBase('filament', id, slot) }}
        onPick={(id) => pickOrOpen('filament', id, (id) => id !== name && requestPreset('filament', slot, id))} footer={[presets('filament', name)]}>
        {pending ? <PickerSkeleton/> : <><span className="pk-type">{first(c.filament_type) || '—'}</span>
        <span className="pk-brand">{b.brand}</span>
        <span className="pk-name">{label}</span></>}
      </Picker>
    </span>
  );
}

export function AddFilamentPicker({ disabled }: { disabled?: boolean }) {
  if (disabled) return null;
  return (
    <Picker label="Add filament" className="is-link" sections memory="orca.web.picker.filament" facetHeads groups={sameStateGroups('filament:add', () => filamentGroups(null))} catalog={{ label: 'Add filament', groups: sameStateGroups('filament-catalog:add', () => filamentGroups(null, true)), onAdd: (id) => createProfileFromBase('filament', id, 0, addSlot) }} onPick={(id) => pickOrOpen('filament', id, addSlot)}
      footer={[presets('filament')]}>
      Add
    </Picker>
  );
}

const POPULAR = [/bambu|bbl/i, /prusa/i, /creality/i, /anycubic/i, /elegoo/i, /voron/i, /qidi/i, /flashforge/i];
const vendorRank = (v: string) => { const i = POPULAR.findIndex((r) => r.test(v)); return i >= 0 ? i : /custom|generic/i.test(v) ? 99 : 50; };

const size = (c: Config) => { const pts = [c.printable_area].flat().map((p) => String(p).split('x').map(Number)); const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  return pts.length > 1 ? `${Math.round(Math.max(...xs) - Math.min(...xs))} × ${Math.round(Math.max(...ys) - Math.min(...ys))} mm` : ''; };
const byVendorRail = (byVendor: Map<string, PickerItem[]>): PickerGroup[] =>
  [...byVendor].sort((a, b) => vendorRank(a[0]) - vendorRank(b[0]) || b[1].length - a[1].length || a[0].localeCompare(b[0]))
    .map(([v, items]) => ({ id: `v:${v}`, title: v, items: items.sort((a, b) => a.label.localeCompare(b.label)) }));

export function PrinterPicker() {
  const printer = useStore((s) => s.printer);
  useStore((s) => s.machines);
  useStore((s) => s.userPresets);
  const connections = useStore((s) => s.printerConnections);
  const profileGroups = (): PickerGroup[] => {
    const byVendor = new Map<string, PickerItem[]>();
    for (const e of listProfiles('machine')) {
      const v = vendorLabel(e.config, e.vendor);
      byVendor.set(v, [...(byVendor.get(v) ?? []), { id: e.key, label: e.mine ? e.name : shortName(e.name), sub: rowDetail(e.name, [{ value: modelLabel(e.config, e.vendor) }, nozzleField(first(e.config.nozzle_diameter))], [v]) || undefined, meta: size(e.config), search: e.name, image: printerCover(e.vendor, e.config) ?? null, status: pickerStatus(connections, e.key) }]);
    }
    return byVendorRail(byVendor);
  };
  const selected = resolvePreset(getState(), 'machine', printer), pending = usePickerPending(!!selected.name);
  return (
    <Picker label="Printer" value={printer} memory="orca.web.picker.printers" groups={sameStateGroups('printer', profileGroups)} onPick={(name) => name !== printer && requestPreset('machine', 0, name)}
      footer={[{ label: 'Add printer', run: () => setState({ presetUx: { kind: 'presets', type: 'machine', add: true } }) }, presets('machine', printer)]}>
      {pending ? <PickerSkeleton/> : <span className="pk-name">{selected.name ?? 'Choose printer'}</span>}
      {!pending && selected.name && !selected.error && (() => { const sub = rowDetail(selected.name, [{ value: modelLabel(selected.config, selected.base?.vendor ?? '') }, nozzleField(first(selected.config.nozzle_diameter))]); return sub && <span className="pk-sub">{sub}</span>; })()}
    </Picker>
  );
}

export function PresetPicker({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  if (type === 'machine') return <PrinterPicker />;
  return <FilamentPicker slot={slot} />;
}
