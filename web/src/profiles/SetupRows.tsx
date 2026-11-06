

import { useState } from 'react';
import { OPTIONS } from '../options';
import { getState, printerPreset, setState, toast, useStore } from '../state/store';
import { undo, undoable } from '../state/history';
import { flushModified } from '../flush-state';
import { Dropdown } from '../components/Dropdown';
import { Glyph } from '../components/Glyph';
import { MenuButton, type MenuEntry } from '../components/Menu';

import './profiles.css';
import { modelKey, nozzleOf, printerModels } from './machines';
import { NozzleSizeFields } from '../multinozzle/NozzleControls';


export function NozzleField() {
  const machine = useStore((s) => printerPreset(s));
  useStore((s) => s.machines);
  useStore((s) => s.userPresets);
  const variants = machine ? printerModels(getState()).get(modelKey(machine)) ?? [] : [];
  const sizes = [...new Set(variants.map(nozzleOf).filter((d) => Number.isFinite(d) && d > 0))].sort((a, b) => a - b);
  if (sizes.length < 2 || !machine) return null;
  return <NozzleSizeFields config={machine.config} sizes={sizes} />;
}



export function PlateTypeField() {
  const machine = useStore((s) => printerPreset(s));
  const bedType = useStore((s) => s.bedType);
  const bedDef = OPTIONS.curr_bed_type;
  const multiBed = machine?.config.support_multi_bed_types;
  if (!(machine?.vendor === 'BBL' || multiBed === true || multiBed === 1 || multiBed === '1') || !bedDef?.enum_values) return null;
  return (
    <div className="opt-line">
      <span className="opt-label">Plate type</span>
      <span className="opt-field">
        <Dropdown className="combo" aria-label="Plate type" value={bedType}
          onValueChange={(value) => undoable('Change plate type', () => setState({ bedType: value }))}
          options={bedDef.enum_values.flatMap((v, i) =>
            machine?.not_support_bed_type?.some((b) => b === v || b === bedDef.enum_labels?.[i])
              ? [] : [{ value: v, label: bedDef.enum_labels?.[i] ?? v }])} />
      </span>
    </div>
  );
}

export function addFilament() {
  void import('../plater/mixed').then((m) => m.addPhysical()).catch((error) => toast(String(error)));
}


export function addSlot(name: string) {
  void import('../plater/mixed').then((m) => m.addPhysical(undefined, name)).catch((error) => toast(String(error)));
}



export function removeSlot(i: number) {
  void import('../plater/mixed').then(({ removeSlotAt }) => {
    const done = removeSlotAt(i + 1);
    if (!done) return;
    const notes = [done.moved ? `${done.moved} object${done.moved === 1 ? '' : 's'} moved to slot ${done.to}` : '', ...done.merged.map((d) => `mix ${d.mix} merged into slot ${d.into}`)].filter(Boolean);
    toast([`Removed filament ${i + 1}`, ...notes].join('; '), { label: 'Undo', run: undo });
  });
}



export function FilamentMenu() {
  const slots = useStore((s) => s.filamentSlots.length);
  const physical = slots - Object.keys(useStore((s) => s.mixes)).length;
  const flushChanged = useStore((st) => flushModified(st));
  const amsPrinter = useStore((st) => st.printerConnections.connections.some((c) => c.printer === st.printer && c.kind === 'bambu-lan') ? st.printer : undefined);
  const [syncing, setSyncing] = useState(false);
  const sync = () => {
    if (!amsPrinter || syncing) return;
    setSyncing(true);
    void import('../plater/amsSync').then((m) => m.syncAms(amsPrinter)).finally(() => setSyncing(false));
  };
  const items: MenuEntry[] = [
    ...(physical > 1 ? [{ label: flushChanged ? 'Flushing volumes (edited)…' : 'Flushing volumes…', action: () => setState({ flushDialog: true }) }] : []),
    ...(amsPrinter ? [{ label: syncing ? 'Syncing from AMS…' : 'Sync from AMS', disabled: syncing, action: sync }] : []),
    ...(physical > 1 ? [{ label: 'Mixed filament…', action: () => setState({ appDialog: 'mixed' }) }] : []),
  ];
  return items.length ? <MenuButton className="icon-btn rail-act" label="Filament tools" items={items}><Glyph name="more"/></MenuButton> : null;
}
