import { useState } from 'react';
import { Popover } from 'radix-ui';
import { getState, toast, useStore } from '../state/store';
import { findProfile } from './identity';
import { changeFilamentColour, colourName, saveFilamentAsColour, slotColour, slotColourOverride, useProfileColour } from './filament-colour';

const PALETTE = ['#FFFFFF', '#B6B6BE', '#52525B', '#17171B', '#D34F56', '#D99245', '#E1C85B', '#4C9970', '#4389B5', '#785EB0', '#B96692', '#8C6A52'];
const normal = (v: string) => /^#?[\da-f]{6}$/i.test(v) ? `#${v.replace('#', '').toUpperCase()}` : null;

export function FilamentColourMenu({ slot }: { slot: number }) {
  useStore((s) => s.slotColours); useStore((s) => s.userPresets); useStore((s) => s.filamentSlots);
  const s = getState(), colour = slotColour(s, slot), override = slotColourOverride(s, slot);
  const source = findProfile(s, 'filament', s.filamentSlots[slot] ?? ''), owned = !!source && !source.archived && source.kind !== 'master';
  const [open, setOpen] = useState(false), [draft, setDraft] = useState(colour), [busy, setBusy] = useState(false);
  const picked = normal(draft), same = picked === normal(colour);
  const run = async (action: () => Promise<unknown>) => {
    setBusy(true);
    try { await action(); setOpen(false); } catch (error) { toast(error instanceof Error ? error.message : String(error)); } finally { setBusy(false); }
  };
  return <Popover.Root open={open} onOpenChange={(next) => { setOpen(next); if (next) setDraft(colour); }}>
    <Popover.Trigger asChild><button type="button" className="color-trigger" style={{ backgroundColor: colour }} data-modified={override ? true : undefined} aria-label={`Filament ${slot + 1} colour: ${colourName(colour)}${override ? ', changed in this project' : ''}`}/></Popover.Trigger>
    <Popover.Portal><Popover.Content className="orca-menu color-popover" aria-label={`Filament ${slot + 1} colour`} side="bottom" align="start" sideOffset={4} collisionPadding={12} onKeyDown={(e) => e.stopPropagation()}>
      <div className="color-heading"><span>{picked ? colourName(picked) : 'Colour'}</span><span className="color-preview" style={{ backgroundColor: picked ?? colour }}/></div>
      <div className="color-palette" role="group" aria-label="Palette">{PALETTE.map((c) => <button key={c} type="button" className="color-swatch" style={{ backgroundColor: c }} aria-label={colourName(c)} aria-pressed={picked === c} onClick={() => setDraft(c)}/>)}</div>
      <label className="color-hex"><span>HEX</span><input className="text" aria-label="HEX colour" aria-invalid={!picked} value={draft} spellCheck={false} maxLength={7} onChange={(e) => setDraft(e.target.value)}/></label>
      <div className="color-actions filament-colour-actions">
        {override && <button type="button" className="btn-ghost" disabled={busy} onClick={() => { useProfileColour(slot); setOpen(false); }}>Use profile colour</button>}
        <button type="button" className="btn" disabled={busy || !picked || same || !owned} onClick={() => void run(() => changeFilamentColour(slot, picked!))}>Change this filament</button>
        <button type="button" className="btn primary" disabled={busy || !picked || same || !source} onClick={() => void run(() => saveFilamentAsColour(slot, picked!))}>Save as new filament</button>
      </div>
    </Popover.Content></Popover.Portal>
  </Popover.Root>;
}
