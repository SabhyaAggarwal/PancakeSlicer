import type { PresetType } from '../api/types';
import { switchPreset } from '../actions';
import { compatible, getState, hasUnsavedEdits, presetEdits, setState, useStore } from '../state/store';

const CREATE = '\u0000create-printer';

export function PresetCombo({ type, slot = 0, className }: { type: PresetType; slot?: number; className?: string }) {
  const value = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot] ?? ''));
  const printer = useStore((s) => s.printer);
  const machines = useStore((s) => s.machines);
  useStore((s) => s.processes.length + s.filaments.length);
  const userPresets = useStore((s) => s.userPresets);
  const dirty = useStore((s) => Object.keys(presetEdits(s, type, slot)).length > 0);

  const s = getState();
  const base = userPresets.find((u) => u.type === 'machine' && u.name === printer)?.inherits ?? printer;
  const systemUnsorted = type === 'machine' ? machines : compatible(s, type, base);
  const chosen = useStore((st) => (type === 'filament' ? st.filamentsToUse : null));
  const offered = chosen && systemUnsorted.some((p) => chosen.includes(p.name)) ? systemUnsorted.filter((p) => p.name === value || chosen.includes(p.name)) : systemUnsorted;
  const system = [...offered].sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
  const sysNames = new Set(systemUnsorted.map((p) => p.name));
  const user = userPresets.filter((u) => u.type === type && (type === 'machine' || sysNames.has(u.inherits))).sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);

  const onChange = (name: string) => {
    if (name === CREATE) return setState({ appDialog: 'createPrinter' });
    if (hasUnsavedEdits(getState(), type, slot)) setState({ presetUx: { kind: 'unsaved', type, slot, target: name } });
    else switchPreset(type, slot, name);
  };

  return (
    <select className={`combo preset-combo ${className ?? ''}`} value={value} onChange={(e) => onChange(e.target.value)} title={value}>
      {!sysNames.has(value) && !user.some((u) => u.name === value) && <option value={value}>{value || '—'}</option>}

      {user.length > 0 && (
        <optgroup label="User presets">
          {user.map((u) => (
            <option key={u.name} value={u.name}>
              {u.name}{u.name === value && dirty ? ' (modified)' : ''}
            </option>
          ))}
        </optgroup>
      )}
      <optgroup label="System presets">
        {system.map((p) => (
          <option key={p.name} value={p.name}>
            {p.name}{p.name === value && dirty ? ' (modified)' : ''}
          </option>
        ))}
      </optgroup>
      {type === 'machine' && <option value={CREATE}>Create printer…</option>}
    </select>
  );
}
