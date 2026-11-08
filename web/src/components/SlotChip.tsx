import { DropdownMenu as Menu } from 'radix-ui';
import { filamentColor } from '../plater/filamentColor';
import { getState, resolvePreset, useStore, type State } from '../state/store';
import './menu.css';

export const inkOn = (hex: string) => {
  const m = /^#?([\da-f]{2})([\da-f]{2})([\da-f]{2})/i.exec(hex);
  return m && 0.2126 * parseInt(m[1], 16) + 0.7152 * parseInt(m[2], 16) + 0.0722 * parseInt(m[3], 16) > 150 ? 'dark' : 'light';
};

export function SlotChip({ n, colour }: { n: number; colour: string }) {
  return <span className="slot-chip" aria-hidden data-ink={inkOn(colour)} style={{ backgroundColor: colour }}>{n}</span>;
}

export const slotsOf = (s: State) => s.filamentSlots.map((name, i) => ({ n: i + 1, colour: filamentColor(s, i + 1), name: resolvePreset(s, 'filament', name).name ?? name }));
export function useSlots() {
  useStore((s) => s.filamentSlots);
  useStore((s) => s.slotColours);
  useStore((s) => s.userPresets);
  return slotsOf(getState());
}

export function SlotPicker({ value, onChange, label, disabled = false, inherit }: {
  value: number;
  onChange(slot: number): void;
  label: string;
  disabled?: boolean;
  inherit?: number;
}) {
  const slots = useSlots();
  const shown = value || inherit || 1, colour = slots[shown - 1]?.colour ?? '#808080';
  const chip = <SlotChip n={shown} colour={colour} />;
  if (slots.length < 2 && !inherit) return <span className="slot-pick" role="img" aria-label={`${label}: slot ${shown}`}>{chip}</span>;
  return (
    <Menu.Root>
      <Menu.Trigger asChild disabled={disabled}>
        <button type="button" className="slot-pick" data-follows={value === 0 && inherit ? '' : undefined} aria-label={`${label}: slot ${shown}${value === 0 && inherit ? ' (the object’s)' : ''}`}
          onClick={(e) => e.stopPropagation()} onPointerDown={(e) => e.stopPropagation()}>{chip}</button>
      </Menu.Trigger>
      <Menu.Portal>
        <Menu.Content className="orca-menu slot-menu" data-menu-family="orca" align="end" sideOffset={4} collisionPadding={8} onClick={(e) => e.stopPropagation()}>
          {inherit !== undefined && (
            <Menu.Item className="menu-item" data-current={value === 0 ? '' : undefined} onSelect={() => onChange(0)}>
              <SlotChip n={inherit} colour={slots[inherit - 1]?.colour ?? '#808080'} /><span className="orca-menu-label">Object’s filament</span>
            </Menu.Item>
          )}
          {slots.map((slot) => (
            <Menu.Item key={slot.n} className="menu-item" data-current={slot.n === value ? '' : undefined} onSelect={() => onChange(slot.n)}>
              <SlotChip n={slot.n} colour={slot.colour} /><span className="orca-menu-label">{slot.name}</span>
            </Menu.Item>
          ))}
        </Menu.Content>
      </Menu.Portal>
    </Menu.Root>
  );
}
