import type { Config, ConfigValue } from '../api/types';
import { Dropdown } from '../components/Dropdown';
import { getState, resolvePreset, toast, useStore, type State } from '../state/store';
import { extruderCount, extruderLabel, replaceAt, valueAt, valuesOf } from '../multinozzle/config';
import { findProfile, profileKey } from './identity';

const TYPES = ['Direct Drive', 'Bowden'];
const split = (variant: string) => { const type = TYPES.find((t) => variant.startsWith(`${t} `)) ?? 'Direct Drive'; return { type, volume: variant.slice(type.length).trim() || 'Standard' }; };
export function extruderChoices(config: Config) {
  const count = extruderCount(config), lists = valuesOf(config.extruder_variant_list);
  const volume = config.nozzle_volume_type ?? config.default_nozzle_volume_type;
  const rows = Array.from({ length: count }, (_, i) => {
    const installed = `${valueAt(config.extruder_type, i, 'Direct Drive')} ${valueAt(volume, i, 'Standard')}`;
    const options = [...new Set([installed, ...(lists[i] ?? '').split(',').map((v) => v.trim()).filter(Boolean)])];
    return { installed, options, nozzle: valueAt(config.nozzle_diameter, i) };
  });
  return count > 1 || rows.some((r) => r.options.length > 1) ? rows : null;
}
export function installVariant(config: Config, i: number, variant: string): Record<string, ConfigValue> {
  const count = extruderCount(config), { type, volume } = split(variant);
  return { nozzle_volume_type: replaceAt(config.nozzle_volume_type ?? config.default_nozzle_volume_type, i, volume, count),
    extruder_type: replaceAt(config.extruder_type, i, type, count) };
}
async function saveOnPrinter(i: number, variant: string) {
  const s = getState(), source = findProfile(s, 'machine', s.printer);
  if (!source || source.archived) throw new Error('Choose a saved printer first.');
  const key = profileKey(source), config = resolvePreset(s, 'machine', key).config;
  const [{ saveProfile }, { commitProfileRecords }] = await Promise.all([import('./model'), import('./transactions')]);
  const record = { ...saveProfile(s, 'machine', source.name, key, { ...config, ...installVariant(config, i, variant) }), id: key };
  await commitProfileRecords([record], { apply: (st: State) => {
    if (st.printer !== key || !('nozzle_volume_type' in st.dirty.machine || 'extruder_type' in st.dirty.machine)) return {};
    const { nozzle_volume_type: _volume, extruder_type: _type, ...rest } = st.dirty.machine;
    return { dirty: { ...st.dirty, machine: rest } };
  } });
}

export function ExtruderRows({ config, onSet, disabled }: { config?: Config; onSet?(key: string, value: ConfigValue): void; disabled?: boolean }) {
  const printer = useStore((s) => s.printer);
  useStore((s) => s.userPresets);
  const s = getState(), source = config ?? resolvePreset(s, 'machine', printer).config, bambu = findProfile(s, 'machine', printer)?.base?.vendor === 'BBL' || resolvePreset(s, 'machine', printer).base?.vendor === 'BBL';
  const rows = extruderChoices(source);
  if (!rows) return null;
  const set = (i: number, variant: string) => {
    if (onSet) { for (const [k, v] of Object.entries(installVariant(source, i, variant))) onSet(k, v); return; }
    void saveOnPrinter(i, variant).catch((error) => toast(error instanceof Error ? error.message : String(error)));
  };
  return <div className="extruder-rows" role="group" aria-label="Extruders">{rows.map((row, i) => {
    const label = rows.length > 1 ? extruderLabel(i, rows.length, bambu).replace(/ nozzle$/, '') : 'Extruder';
    return <div key={i} className="extruder-row">
      <span className="extruder-name">{label}</span>
      {row.options.length > 1 ? <Dropdown className="combo" aria-label={`${label} extruder`} disabled={disabled} value={row.installed}
        options={row.options.map((value) => ({ value, label: value }))} onValueChange={(value) => value !== row.installed && set(i, value)}/>
        : <span className="extruder-variant">{row.installed}</span>}
      {row.nozzle && <span className="extruder-nozzle">{row.nozzle} mm</span>}
    </div>;
  })}</div>;
}
