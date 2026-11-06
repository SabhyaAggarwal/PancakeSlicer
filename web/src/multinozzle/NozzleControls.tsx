import type { Config, ConfigValue, PresetType } from '../api/types';
import { Dropdown } from '../components/Dropdown';
import { effectiveConfig, getState, printerPreset, setOption, useStore } from '../state/store';
import { assignedExtruder, nozzleAssignments, setNozzleAssignment, updateAssignments, VOLUME_TYPES } from './assignments';
import { extruderCount, extruderLabel, replaceAt, valueAt, valuesOf, variantIndex, variantsOf, type ExtruderSelection } from './config';

export function VariantSelector({ type, config, machine, selection, onChange }: {
  type: PresetType; config: Config; machine: Config; selection: ExtruderSelection; onChange(value: ExtruderSelection): void;
}) {
  const count = extruderCount(machine), variants = variantsOf(config, type);
  const bambu = useStore((s) => printerPreset(s)?.vendor === 'BBL');
  const ids = valuesOf(config[type === 'machine' ? 'printer_extruder_id' : 'print_extruder_id']);
  const options = type === 'filament' ? variants.map((name, i) => ({ value: String(i), label: name })) :
    Array.from({ length: count }, (_, extruder) => {
      const indices = variants.map((_, i) => i).filter((i) => Number(ids[i] ?? 1) === extruder + 1);
      return (indices.length ? indices : [0]).map((variant) => ({ value: `${extruder}:${variant}`, label: `${extruderLabel(extruder, count, bambu)}${indices.length > 1 ? ` · ${variants[variant]}` : ''}` }));
    }).flat();
  if (options.length < 2) return null;
  const variant = variantIndex(config, type, selection);
  return <div className="opt-line"><span className="opt-label">{type === 'filament' ? 'Extruder variant' : 'Extruder'}</span><span className="opt-field">
    <Dropdown className="combo" aria-label="Edit extruder settings" value={type === 'filament' ? String(variant) : `${Math.min(selection.extruder, count - 1)}:${variant}`}
      options={options} onValueChange={(value) => { const [first, second] = value.split(':').map(Number); onChange(type === 'filament' ? { extruder: 0, variant: first } : { extruder: first, variant: second }); }} />
  </span></div>;
}


export function FilamentNozzleAssignment({ slot, disabled }: { slot: number; disabled?: boolean }) {
  const s = useStore((state) => state);
  const machine = effectiveConfig(s, 'machine'), count = extruderCount(machine);
  if (count < 2) return null;
  const config = nozzleAssignments(s), extruder = assignedExtruder(s, slot), mode = String(config.filament_map_mode ?? 'Auto For Flush');
  const bambu = printerPreset(s)?.vendor === 'BBL';
  const flow = valueAt(machine.nozzle_volume_type ?? machine.default_nozzle_volume_type, extruder, 'Standard');
  const hybrid = flow === 'Hybrid';
  const volume = valueAt(config.filament_volume_map, slot, '0');
  return <>
    <div className="opt-line"><span className="opt-label">Nozzle mapping</span><span className="opt-field">
      <Dropdown className="combo" aria-label="Nozzle mapping" disabled={disabled} value={mode} options={['Auto For Flush', 'Auto For Match', 'Manual', ...(mode === 'Nozzle Manual' ? ['Nozzle Manual'] : [])].map((value) => ({ value, label: value }))}
        onValueChange={(value) => value === 'Manual' ? setNozzleAssignment(slot, Math.max(0, Math.min(count - 1, extruder))) : updateAssignments({ filament_map_mode: value })} />
    </span></div>
    <div className="opt-line"><span className="opt-label">Filament {slot + 1} nozzle</span><span className="opt-field">
      <Dropdown className="combo" aria-label={`Filament ${slot + 1} nozzle`} disabled={disabled} value={String(extruder)}
        options={Array.from({ length: count }, (_, i) => ({ value: String(i), label: `${extruderLabel(i, count, bambu)} · ${valueAt(machine.nozzle_diameter, i)} mm` }))}
        onValueChange={(value) => setNozzleAssignment(slot, Number(value))} />
    </span></div>
    {hybrid && <div className="opt-line"><span className="opt-label">Nozzle flow</span><span className="opt-field">
      <Dropdown className="combo" aria-label={`Filament ${slot + 1} nozzle flow`} disabled={disabled} value={volume}
        options={Object.entries(VOLUME_TYPES).filter(([name]) => name !== 'Hybrid').map(([label, value]) => ({ value: String(value), label }))}
        onValueChange={(value) => setNozzleAssignment(slot, extruder, Number(value))} />
    </span></div>}
  </>;
}

export function NozzleSizeFields({ config, sizes }: { config: Config; sizes: number[] }) {
  const count = extruderCount(config), bambu = useStore((s) => printerPreset(s)?.vendor === 'BBL');
  return <>{Array.from({ length: count }, (_, i) => <div className="opt-line" key={i}>
    <span className="opt-label">{count === 1 ? 'Nozzle' : extruderLabel(i, count, bambu)}</span><span className="opt-field">
      <Dropdown className="combo" aria-label={count === 1 ? 'Nozzle' : `${extruderLabel(i, count, bambu)} diameter`} value={valueAt(config.nozzle_diameter, i)}
        options={[...new Set([...sizes.map(String), valueAt(config.nozzle_diameter, i)])].map((value) => ({ value, label: `${value} mm` }))}
        onValueChange={(value) => setOption('machine', 'nozzle_diameter', replaceAt(effectiveConfig(getState(), 'machine').nozzle_diameter, i, value, count))} />
    </span>
  </div>)}</>;
}

export function NozzleFlowFields({ config, onSet, disabled }: { config: Config; onSet?(key: string, value: ConfigValue): void; disabled?: boolean }) {
  const count = extruderCount(config), bambu = useStore((s) => printerPreset(s)?.vendor === 'BBL');
  const installed = config.nozzle_volume_type ?? config.default_nozzle_volume_type;
  const lists = valuesOf(config.extruder_variant_list);
  if (count < 2 && !lists.some((v) => v.includes(','))) return null;
  return <>{Array.from({ length: count }, (_, i) => {
    const allowed = (lists[i] ?? '').split(',').map((v) => v.replace(/^(Direct Drive|Bowden) /, ''));
    const value = valueAt(installed, i, 'Standard');
    const options = [...new Set([value, ...allowed.filter((v) => v in VOLUME_TYPES)])];
    if (options.length < 2) return null;
    return <div className="opt-line" key={i}><span className="opt-label">{count === 1 ? 'Nozzle flow' : `${extruderLabel(i, count, bambu)} flow`}</span><span className="opt-field">
      <Dropdown className="combo" aria-label={`${extruderLabel(i, count, bambu)} flow`} value={value} disabled={disabled}
        options={options.map((value) => ({ value, label: value }))} onValueChange={(value) => (onSet ?? ((key, next) => setOption('machine', key, next)))('nozzle_volume_type', replaceAt(installed, i, value, count))} />
    </span></div>;
  })}</>;
}
