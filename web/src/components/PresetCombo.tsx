import type { PresetType } from '../api/types';
import { choosePrinter } from '../actions';
import { compatible, getState, presetEdits, setState, useStore } from '../state/store';
import { undoable } from '../state/history';

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
  const system = [...systemUnsorted].sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
  const sysNames = new Set(system.map((p) => p.name));
  const user = userPresets.filter((u) => u.type === type && (type === 'machine' || sysNames.has(u.inherits))).sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);

  const onChange = (name: string) => {
    if (type !== 'filament' && dirty && !window.confirm('The current preset has unsaved changes. Discard them?')) return;
    if (type === 'machine') {
      undoable('Change printer', () => choosePrinter(name));
      return;
    }
    undoable(type === 'process' ? 'Change process preset' : 'Change filament preset', () => setState((st) => ({
      dirty: type === 'process' ? { ...st.dirty, process: {} } : st.dirty,
      projectPresets: st.projectPresets && (type === 'process'
        ? { ...st.projectPresets, process: {} }
        : { ...st.projectPresets, filaments: st.projectPresets.filaments.map((f, i) => i === slot ? {} : f) }),
      ...(type === 'process' ? { process: name } : { filamentSlots: st.filamentSlots.map((f, i) => (i === slot ? name : f)) }),
      job: st.job.state === 'done' ? { ...st.job, stale: true } : st.job,
    })));
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
    </select>
  );
}
