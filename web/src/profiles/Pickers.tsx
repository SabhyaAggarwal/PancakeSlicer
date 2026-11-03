import { printerLine, profileColour, vendorLabel } from './metadata';
import type { Config, PresetType } from '../api/types';
import { filamentSwitchReason, getState, resolvePreset, setState, useStore } from '../state/store';
import { Picker, type PickerAction, type PickerGroup, type PickerItem } from '../components/picker/Picker';
import { requestPreset } from '../components/PresetCombo';
import { filamentColor } from '../plater/filamentColor';
import { addSlot } from './SetupRows';
import { FilamentColourMenu } from './FilamentColourMenu';
import { createProfileFromBase, listCatalog, listProfiles, type PickerEntry } from './pickerSource';

const first = (v: unknown) => String([v].flat()[0] ?? '');
const shortName = (name: string) => name.replace(/\s*@.*$/, '') || name;
export const presets = (type: PresetType, name?: string): PickerAction => ({ label: `Manage ${type === 'machine' ? 'printers' : type === 'filament' ? 'filaments' : 'print settings'}`, run: () => setState({ presetUx: { kind: 'presets', type, name } }) });

function brandName(name: string, c: Config) {
  const brand = first(c.filament_vendor) || 'Generic', short = shortName(name);
  const word = brand.split(/\s+/)[0];
  return { brand, name: short.toLowerCase().startsWith(word.toLowerCase() + ' ') ? short.slice(word.length + 1) : short };
}

const MATERIALS = ['PLA', 'PETG', 'ABS', 'TPU', 'ASA', 'PC', 'PA', 'PVA'];
const RARE = /^(PVA|BVOH|HIPS|EVA|COPE|PVB|SUPPORT|OTHER)$/;
const material = (type: string) => (type.toUpperCase().split(/[-+\s]/)[0] || 'Other').replace(/^PA\d+$/, 'PA');

function filamentGroups(slot: number | null, catalog = false): PickerGroup[] {
  const s = getState();
  const entries = catalog ? listCatalog('filament') : listProfiles('filament');
  const item = (e: PickerEntry, swatch?: string | null, sub?: string): PickerItem => {
    const brand = first(e.config.filament_vendor) || e.vendor || 'Generic', b = brandName(e.name, e.config);
    return { id: e.key, label: e.mine ? e.name : b.name, sub: sub ?? brand, facet: brand, swatch:swatch ?? profileColour(e.config), disabled: e.reason ?? false, search: `${brand} ${first(e.config.filament_type)} ${e.name}` };
  };
  const loaded = catalog ? [] : s.filamentSlots.flatMap((name, i) => {
    const reason = filamentSwitchReason(s, i), e = entries.find((x) => x.key === name);
    if ((i === slot && !reason) || !e) return [];
    return [{ ...item(e, filamentColor(s, i + 1), `Slot ${i + 1}`), disabled: reason ?? false }];
  }).filter((it, i, all) => all.findIndex((x) => x.id === it.id) === i);
  const seen = new Map<string, number>();
  for (const e of entries) { const k = item(e).label + first(e.config.filament_vendor); seen.set(k, (seen.get(k) ?? 0) + 1); }
  const types = new Map<string, PickerItem[]>();
  for (const e of entries) {
    const it = item(e), at = e.name.match(/@\s*(.+)$/)?.[1];
    if (!e.mine && at && !/system/i.test(at) && seen.get(it.label + first(e.config.filament_vendor))! > 1) it.label += ` · ${at.replace(/^BBL\s+/, '')}`;
    const type = material(first(e.config.filament_type));
    types.set(type, [...(types.get(type) ?? []), it]);
  }
  const rank = (t: string) => { const i = MATERIALS.indexOf(t); return i >= 0 ? i : RARE.test(t) ? 99 : 50; };
  const groups: PickerGroup[] = [];
  if (loaded.length) groups.push({ id: 'loaded', title: 'Loaded', items: loaded, lead: true });
  for (const type of [...types.keys()].sort((a, b) => rank(a) - rank(b) || a.localeCompare(b)))
    groups.push({ id: `t:${type}`, title: type, items: types.get(type)!.sort((a, b) => a.facet!.localeCompare(b.facet!) || a.label.localeCompare(b.label)) });
  return groups;
}

export function FilamentPicker({ slot }: { slot: number }) {
  const name = useStore((s) => s.filamentSlots[slot] ?? '');
  useStore((s) => s.filamentEdits);
  useStore((s) => s.userPresets);
  const s = getState();
  const resolved = resolvePreset(s, 'filament', name), c = resolved.config, label = resolved.name ?? 'Unavailable filament';
  const b = brandName(label, c);
  return (
    <span className="pk-slot">
      <FilamentColourMenu slot={slot}/>
      <Picker label={`Filament ${slot + 1}`} value={name} memory="orca.web.picker.filament" facetLabel="Vendor" groups={() => filamentGroups(slot)}
        catalog={{ label: 'Add filament', groups: () => filamentGroups(slot, true), onAdd: (id) => createProfileFromBase('filament', id, slot) }}
        onPick={(id) => id !== name && requestPreset('filament', slot, id)} footer={[presets('filament', name)]}>
        <span className="pk-type">{first(c.filament_type) || '—'}</span>
        <span className="pk-brand">{b.brand}</span>
        <span className="pk-name">{label}</span>
      </Picker>
    </span>
  );
}

export function AddFilamentPicker({ disabled }: { disabled?: boolean }) {
  if (disabled) return null;
  return (
    <Picker label="Add filament" className="is-link" memory="orca.web.picker.filament" facetLabel="Vendor" groups={() => filamentGroups(null)} catalog={{ label: 'Add filament', groups: () => filamentGroups(null, true), onAdd: (id) => createProfileFromBase('filament', id, 0, addSlot) }} onPick={(id) => addSlot(id)}
      footer={[presets('filament')]}>
      Add
    </Picker>
  );
}

const POPULAR = [/bambu|bbl/i, /prusa/i, /creality/i, /anycubic/i, /elegoo/i, /voron/i, /qidi/i, /flashforge/i];
const vendorRank = (v: string) => { const i = POPULAR.findIndex((r) => r.test(v)); return i >= 0 ? i : /custom|generic/i.test(v) ? 99 : 50; };

const size = (c: Config) => { const pts = [c.printable_area].flat().map((p) => String(p).split('x').map(Number)); const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  return pts.length > 1 ? `${Math.round(Math.max(...xs) - Math.min(...xs))}×${Math.round(Math.max(...ys) - Math.min(...ys))}` : ''; };
const byVendorRail = (byVendor: Map<string, PickerItem[]>): PickerGroup[] =>
  [...byVendor].sort((a, b) => vendorRank(a[0]) - vendorRank(b[0]) || b[1].length - a[1].length || a[0].localeCompare(b[0]))
    .map(([v, items]) => ({ id: `v:${v}`, title: v, items: items.sort((a, b) => a.label.localeCompare(b.label)) }));

export function PrinterPicker() {
  const printer = useStore((s) => s.printer);
  useStore((s) => s.machines);
  useStore((s) => s.userPresets);
  const profileGroups = (): PickerGroup[] => {
    const byVendor = new Map<string, PickerItem[]>();
    for (const e of listProfiles('machine')) {
      const v = vendorLabel(e.config, e.vendor);
      byVendor.set(v, [...(byVendor.get(v) ?? []), { id: e.key, label: e.mine ? e.name : shortName(e.name), sub: printerLine(e.config, e.vendor), meta: size(e.config), search: e.name }]);
    }
    return byVendorRail(byVendor);
  };
  const selected = resolvePreset(getState(), 'machine', printer);
  return (
    <Picker label="Printer" value={printer} memory="orca.web.picker.printers" groups={profileGroups} onPick={(name) => name !== printer && requestPreset('machine', 0, name)}
      footer={[{ label: 'Add printer', run: () => setState({ presetUx: { kind: 'presets', type: 'machine', add: true } }) }, presets('machine', printer)]}>
      <span className="pk-name">{selected.name ?? 'Choose printer'}</span>
      {selected.name && !selected.error && <span className="pk-sub">{printerLine(selected.config, selected.base?.vendor ?? '')}</span>}
    </Picker>
  );
}

export function PresetPicker({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  if (type === 'machine') return <PrinterPicker />;
  return <FilamentPicker slot={slot} />;
}
