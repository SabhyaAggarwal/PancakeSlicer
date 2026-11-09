import type { Config, PresetType } from '../api/types';
import { Dropdown } from '../components/Dropdown';
import { effectiveConfig, getState, printerPreset, setOption, useStore } from '../state/store';
import { assignedExtruder, nozzleAssignments, setNozzleAssignment, updateAssignments, VOLUME_TYPES } from './assignments';
import { extruderCount, extruderLabel, installedVariant, replaceAt, valueAt, valuesOf, type ExtruderSelection } from './config';
import { filament_options_with_variant, printer_extruder_options, printer_options_with_variant_1, print_options_with_variant } from './variant-keys';
import { useEffect } from 'react';
import { Choice } from '../components/kit';
import { SEP } from '../separator';

export function VariantSelector({ type, config, machine, selection, onChange }: {
  type: PresetType; config: Config; machine: Config; selection: ExtruderSelection; onChange(value: ExtruderSelection): void;
}) {
  const count = extruderCount(machine);
  const installed = Array.from({ length: count }, (_, e) => installedVariant(config, type, machine, e));
  const extruder = Math.min(selection.extruder, count - 1);
  useEffect(() => { if (selection.extruder !== extruder || selection.variant !== installed[extruder]) onChange({ extruder, variant: installed[extruder] }); });
  if (count < 2) return null;
  const keys = type === 'process' ? print_options_with_variant : type === 'filament' ? filament_options_with_variant : printer_options_with_variant_1;
  const at = (key: string, e: number) => type === 'machine' && printer_extruder_options.has(key) ? e : installed[e];
  const differs = [...keys, ...(type === 'machine' ? printer_extruder_options : [])].some((key) => {
    if (!Array.isArray(config[key])) return false;
    return new Set(installed.map((_, e) => valueAt(config[key], at(key, e)))).size > 1;
  });
  if (!differs) return null;
  const bambu = String(machine.printer_model ?? '').startsWith('Bambu') || valuesOf(machine.extruder_variant_list).length > 0;
  return <div className="opt-line extruder-switch"><span className="opt-label">Values for</span><span className="opt-field">
    <Choice label="Extruder" value={String(extruder)} options={installed.map((_, e) => ({ id: String(e), label: extruderLabel(e, count, bambu).replace(/ nozzle$/, '') }))}
      onChange={(id) => onChange({ extruder: Number(id), variant: installed[Number(id)] })}/>
  </span></div>;
}

const MAP_MODE_LABELS: Record<string, string> = { 'Auto For Flush': 'Auto (best for flushing)', 'Auto For Match': 'Auto (best match)', 'Nozzle Manual': 'Manual nozzle selection' };
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
      <Dropdown className="combo" aria-label="Nozzle mapping" disabled={disabled} value={mode} options={['Auto For Flush', 'Auto For Match', 'Manual', ...(mode === 'Nozzle Manual' ? ['Nozzle Manual'] : [])].map((value) => ({ value, label: MAP_MODE_LABELS[value] ?? value }))}
        onValueChange={(value) => value === 'Manual' ? setNozzleAssignment(slot, Math.max(0, Math.min(count - 1, extruder))) : updateAssignments({ filament_map_mode: value })} />
    </span></div>
    <div className="opt-line"><span className="opt-label">Filament {slot + 1} nozzle</span><span className="opt-field">
      <Dropdown className="combo" aria-label={`Filament ${slot + 1} nozzle`} disabled={disabled} value={String(extruder)}
        options={Array.from({ length: count }, (_, i) => ({ value: String(i), label: `${extruderLabel(i, count, bambu)}${SEP}${valueAt(machine.nozzle_diameter, i)} mm` }))}
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
