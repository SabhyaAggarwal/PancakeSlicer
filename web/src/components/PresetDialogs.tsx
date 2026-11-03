import { useEffect, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType } from '../api/types';
import { normalizeConfigValue, toolValue } from '../config-values';
import { loadTab, OPTIONS, splitKey, TABS, isHidden } from '../options';
import { useMachine } from '../flow/fsm';
import { editing } from '../profiles/editing';
import { compatible, getState, listFor, resolvePreset, setState, useStore, type State } from '../state/store';
import { Modal, ModalTitle } from './Modal';
import { display } from '../profiles/format';

export { display };

const TAB = { machine: 'printer', filament: 'filament', process: 'process' } as const;

const currentName = (s: State, type: PresetType, slot: number) =>
  (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';

export interface DiffRow { key: string; label: string; group: string; left?: ConfigValue; right?: ConfigValue; differs: boolean }

export function diffRows(type: PresetType, left: Config, right: Config, all = false): DiffRow[] {
  const rows: DiffRow[] = [];
  const seen = new Set<string>();
  const differs = (k: string) => {
    const a = left[k] ?? (OPTIONS[k]?.default as ConfigValue | undefined), b = right[k] ?? (OPTIONS[k]?.default as ConfigValue | undefined);
    return (a === undefined ? '' : toolValue(k, normalizeConfigValue(k, a))) !== (b === undefined ? '' : toolValue(k, normalizeConfigValue(k, b)));
  };
  for (const page of TABS[TAB[type]]?.pages ?? []) for (const group of page.groups) for (const line of group.lines) for (const k of line.options) {
    const key = splitKey(k)[0];
    if (seen.has(key) || !OPTIONS[key] || isHidden(key)) continue;
    seen.add(key);
    const d = differs(key);
    if (d || all) rows.push({ key, label: OPTIONS[key].label || line.label || key, group: `${page.title} › ${group.title}`, left: left[key], right: right[key], differs: d });
  }
  for (const key of new Set([...Object.keys(left), ...Object.keys(right)]))
    if (!seen.has(key) && OPTIONS[key] && !isHidden(key) && differs(key))
      rows.push({ key, label: OPTIONS[key].label || key, group: 'Other', left: left[key], right: right[key], differs: true });
  return rows;
}

export const useTabLoaded = (type: PresetType) => {
  const [ready, setReady] = useState(!!TABS[TAB[type]]);
  useEffect(() => void loadTab(TAB[type]).then(() => setReady(true), () => setReady(true)), [type]);
  return ready;
};

const UNUSABLE = /[<>[\]:/\\|?*"]/;

export function SaveDialog({ type, slot }: { type: PresetType; slot: number }) {
  const s = getState();
  const current = currentName(s, type, slot);
  const { user, base } = resolvePreset(s, type, current);
  const [name, setName] = useState(user ? current : `${current.replace(/\s*@.*$/, '')} - Mine`);
  const [detach, setDetach] = useState(false);
  const userPresets = useStore((st) => st.userPresets);
  const m = editing(type, slot);
  const state = useMachine(m, (x) => x.value);
  useEffect(() => { if (m.get().value !== 'naming') m.send({ type: 'SAVE_AS' }); }, [m]);
  const named = useRef(false);
  useEffect(() => {
    if (state === 'naming' || state === 'saving' || state === 'failed') named.current = true;
    else if (named.current) setState({ presetUx: null });
  }, [state]);
  const close = () => { m.send({ type: 'CANCEL' }); setState({ presetUx: null }); };
  const busy = state === 'saving';

  const trimmed = name;
  const existingUser = userPresets.find((u) => u.type === type && u.name === trimmed);
  let error = '', warning = '';
  if (UNUSABLE.test(trimmed)) error = 'Invalid characters';
  else if (trimmed.includes('(modified)')) error = 'Invalid suffix';
  else if (['Default Setting', 'Default Printer', 'Default Filament'].includes(trimmed)) error = 'Reserved name';
  else if (listFor(s, type).some((p) => p.name === trimmed)) error = 'System preset';
  else if (!trimmed) error = 'Empty name';
  else if (trimmed.startsWith(' ')) error = 'Leading space';
  else if (trimmed.endsWith(' ')) error = 'Trailing space';
  else if (trimmed.length > 200) error = 'Too long';
  else if (existingUser && trimmed !== current) {
    const fits = type === 'machine' || compatible(s, type).some((p) => p.name === existingUser.inherits);
    warning = fits ? 'Overwrites' : 'Overwrites, other printer';
  }

  const save = () => { if (!error && !busy) m.send({ type: 'NAMED', name: trimmed, detach }); };
  return (
    <Modal className="np-dialog small save-dialog" label="Save preset" onClose={close}>
      <ModalTitle title="Save preset" onClose={close} />
      <form className="np-dialog-body" onSubmit={(e) => { e.preventDefault(); save(); }}>
        <label className="np-field">
          <span>Name</span>
          <input className="text" data-autofocus autoFocus value={name} onChange={(e) => setName(e.target.value)} aria-invalid={!!error} />
        </label>
        <div className="np-field"><span>Parent</span><span>{base?.name ?? '—'}</span></div>
        <label className="row">
          <input type="checkbox" checked={detach} onChange={(e) => setDetach(e.target.checked)} />
          Detach
        </label>
        {error && <div className="np-error" role="alert">{error}</div>}
        {warning && <div className="np-warning" role="status">{warning}</div>}
        {state === 'failed' && <div className="np-error" role="alert">{m.get().context.error}</div>}
        <button type="submit" hidden />
      </form>
      <div className="modal-buttons np-buttons">
        <button type="button" className="btn primary" disabled={!!error || busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        <button type="button" className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}
