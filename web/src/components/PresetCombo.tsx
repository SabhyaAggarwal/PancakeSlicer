import type { PresetType } from '../api/types';
import { switchPreset } from '../actions';
import { getState, toast } from '../state/store';
import { reviewChanges } from '../profiles/PresetBar';

export function requestPreset(type: PresetType, slot: number, name: string) {
  const { kept, dropped } = switchPreset(type, slot, name, 'transfer');
  if (!kept && !dropped) return;
  let reviewType = type, reviewSlot = slot;
  if (type === 'machine') {
    const s = getState();
    if (!Object.keys(s.dirty.machine).length) {
      reviewType = Object.keys(s.dirty.process).length ? 'process' : 'filament';
      reviewSlot = Math.max(0, s.projectPresets?.filaments.findIndex((f) => Object.keys(f).length) ?? 0);
    }
  }
  toast(`${kept} changes kept${dropped ? ` · ${dropped} unavailable` : ''} ·`, {
    label: 'Review', run: () => reviewChanges(reviewType, reviewSlot),
  });
}

import { pickColour } from '../profiles/SetupRows';
import { PrinterPicker, FilamentPicker } from '../profiles/Pickers';
import { ProcessPresetPicker } from './ProcessPresetPicker';
export function PresetCombo({ type, slot = 0, className }: { type: PresetType; slot?: number; className?: string }) {
  return <span className={`preset-combo ${className ?? ''}`}>{type === 'machine' ? <PrinterPicker /> : type === 'process' ? <ProcessPresetPicker /> : <FilamentPicker slot={slot} onColour={(c) => pickColour(slot, c)} />}</span>;
}
