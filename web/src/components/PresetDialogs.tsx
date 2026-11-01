import { useEffect, useMemo, useState } from 'react';
import { accessSnapshot } from '../api/access';
import type { Config, ConfigValue, PresetType } from '../api/types';
import { normalizeConfigValue, toolValue } from '../config-values';
import { loadTab, OPTIONS, splitKey, TABS, isHidden } from '../options';
import { switchPreset } from '../actions';
import { compatible, effectiveConfig, getState, listFor, presetEdits, resolvePreset, saveUserPreset, setState, toast, useStore, type State } from '../state/store';
import { Modal, ModalTitle } from './Modal';

const TAB = { machine: 'printer', filament: 'filament', process: 'process' } as const;
const TYPE_NAME = { machine: 'Printer', filament: 'Filament', process: 'Process' } as const;

const currentName = (s: State, type: PresetType, slot: number) =>
  (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';

export function display(key: string, value: ConfigValue | undefined): string {
  if (value === undefined) return '—';
  const def = OPTIONS[key];
  const v = normalizeConfigValue(key, value);
  const one = (x: unknown) => {
    const text = String(x);
    if (def?.type.startsWith('bool')) return text === '1' || text === 'true' ? 'true' : 'false';
    const i = def?.enum_values?.indexOf(text) ?? -1;
    return i >= 0 ? def!.enum_labels?.[i] ?? text : text;
  };
  return Array.isArray(v) ? v.map(one).join(', ') : one(v);
}

interface DiffRow { key: string; label: string; group: string; left?: ConfigValue; right?: ConfigValue }

function diffRows(type: PresetType, left: Config, right: Config): DiffRow[] {
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
    if (differs(key)) rows.push({ key, label: OPTIONS[key].label || line.label || key, group: `${page.title} : ${group.title}`, left: left[key], right: right[key] });
  }
  for (const key of new Set([...Object.keys(left), ...Object.keys(right)]))
    if (!seen.has(key) && OPTIONS[key] && !isHidden(key) && differs(key))
      rows.push({ key, label: OPTIONS[key].label || key, group: 'Other', left: left[key], right: right[key] });
  return rows;
}

function DiffTable({ rows, left, right }: { rows: DiffRow[]; left: string; right: string }) {
  let group = '';
  return (
    <div className="diff-scroll">
      <table className="np-table diff-table">
        <thead><tr><th>Option</th><th>{left}</th><th>{right}</th></tr></thead>
        <tbody>{rows.flatMap((r) => {
          const out = [];
          if (r.group !== group) { group = r.group; out.push(<tr key={`g:${group}`} className="group-row"><td colSpan={3}>{group}</td></tr>); }
          out.push(<tr key={r.key} title={`parameter name: ${r.key}`}><td>{r.label}</td><td>{display(r.key, r.left ?? OPTIONS[r.key]?.default as ConfigValue)}</td>
            <td className="diff-new">{display(r.key, r.right ?? OPTIONS[r.key]?.default as ConfigValue)}</td></tr>);
          return out;
        })}</tbody>
      </table>
    </div>
  );
}

const useTabLoaded = (type: PresetType) => {
  const [ready, setReady] = useState(!!TABS[TAB[type]]);
  useEffect(() => void loadTab(TAB[type]).then(() => setReady(true), () => setReady(true)), [type]);
  return ready;
};

const UNUSABLE = /[<>[\]:/\\|?*"]/;

export function SaveDialog({ type, slot, then }: { type: PresetType; slot: number; then?: string }) {
  const s = getState();
  const current = currentName(s, type, slot);
  const { base, user } = resolvePreset(s, type, current);
  const [name, setName] = useState(user ? current : `${current} - Copy`);
  const [detach, setDetach] = useState(false);
  const [busy, setBusy] = useState(false);
  const userPresets = useStore((st) => st.userPresets);
  const close = () => setState({ presetUx: null });

  const trimmed = name;
  const existingUser = userPresets.find((u) => u.type === type && u.name === trimmed);
  let error = '', warning = '';
  if (UNUSABLE.test(trimmed)) error = 'Name is invalid; illegal characters: <>[]:/\\|?*"';
  else if (trimmed.includes('(modified)')) error = 'Name is invalid; illegal suffix: (modified)';
  else if (['Default Setting', 'Default Printer', 'Default Filament'].includes(trimmed)) error = 'Name is unavailable.';
  else if (listFor(s, type).some((p) => p.name === trimmed)) error = 'Overwriting a system profile is not allowed.';
  else if (!trimmed) error = 'The name field is not allowed to be empty.';
  else if (trimmed.startsWith(' ')) error = 'The name is not allowed to start with a space.';
  else if (trimmed.endsWith(' ')) error = 'The name is not allowed to end with a space.';
  else if (trimmed.length > 200) error = 'The name is longer than 200 characters.';
  else if (existingUser && trimmed !== current) {
    const fits = type === 'machine' || compatible(s, type).some((p) => p.name === existingUser.inherits);
    warning = `Preset "${trimmed}" already exists${fits ? '' : ' and is incompatible with the current printer'}.\nPlease note that saving will overwrite the current preset.`;
  }

  const save = async () => {
    if (error || busy) return;
    setBusy(true);
    try {
      await saveUserPreset(type, trimmed, slot, detach);
    } catch (e) {
      setBusy(false);
      const message = e instanceof Error ? e.message : String(e);
      if (accessSnapshot()?.message !== message) toast(`Saving the preset failed: ${message}`);
      return;
    }
    toast(`Preset "${trimmed}" saved`);
    close();
    if (then) switchPreset(type, slot, then);
  };
  return (
    <Modal className="np-dialog small save-dialog" label="Save preset" onClose={close}>
      <ModalTitle title="Save preset" onClose={close} />
      <form className="np-dialog-body" onSubmit={(e) => { e.preventDefault(); void save(); }}>
        <label className="np-field">
          <span>Save {TYPE_NAME[type].toLowerCase()} preset as:</span>
          <input className="text" data-autofocus autoFocus value={name} onChange={(e) => setName(e.target.value)} aria-invalid={!!error} />
        </label>
        <div className="np-parent"><span className="hint-text">Parent preset</span> {base?.name ?? 'Unique preset'}</div>
        <label className="row" title="Copies all inherited values from the parent into this preset and removes the parent relationship. Presets compatible only with the parent may become unsupported.">
          <input type="checkbox" checked={detach} onChange={(e) => setDetach(e.target.checked)} />
          Detach from parent
        </label>
        {error && <div className="np-error" role="alert">{error}</div>}
        {warning && <div className="np-warning" role="status" style={{ whiteSpace: 'pre-line' }}>{warning}</div>}
        <button type="submit" hidden />
      </form>
      <div className="modal-buttons np-buttons">
        <button type="button" className="btn primary" disabled={!!error || busy} onClick={() => void save()}>Save</button>
        <button type="button" className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}

export function UnsavedDialog({ type, slot, target }: { type: PresetType; slot: number; target: string }) {
  const ready = useTabLoaded(type);
  const s = getState();
  const current = currentName(s, type, slot);
  const edits = presetEdits(s, type, slot);
  const saved = resolvePreset(s, type, current).config;
  const rows = useMemo(() => (ready ? diffRows(type, saved, { ...saved, ...edits }) : []), [ready]);
  const close = () => setState({ presetUx: null });
  const act = (how: 'transfer' | 'discard') => { close(); switchPreset(type, slot, target, how); };
  return (
    <Modal className="np-dialog unsaved-dialog" label="Unsaved changes" onClose={close}>
      <ModalTitle title="Unsaved changes" onClose={close} />
      <div className="np-dialog-body">
        {ready ? <DiffTable rows={rows} left="Old Value" right="New Value" /> : <div role="status">Loading settings…</div>}
      </div>
      <div className="modal-buttons np-buttons">
        <button type="button" className="btn" title={`All "New Value" settings modified in "${current}" will be transferred to "${target}".`} data-autofocus onClick={() => act('transfer')}>Transfer</button>
        <button type="button" className="btn" title={`Switch to "${target}" discarding any changes made in "${current}".`} onClick={() => act('discard')}>Discard</button>
        <button type="button" className="btn primary" title="Save the modified values as a preset, then switch"
          onClick={() => setState({ presetUx: { kind: 'save', type, slot, then: target } })}>Save</button>
        <span className="spacer" />
        <button type="button" className="btn" onClick={close}>Cancel</button>
      </div>
    </Modal>
  );
}

function presetNames(s: State, type: PresetType) {
  return [...s.userPresets.filter((u) => u.type === type).map((u) => u.name).sort(), ...listFor(s, type).map((p) => p.name).sort()];
}

export function CompareDialog({ type: initialType, left: initialLeft, right: initialRight }: { type: PresetType; left?: string; right?: string }) {
  const [type, setType] = useState(initialType);
  const s = getState();
  const names = presetNames(s, type);
  const slot = s.dialog?.type === 'filament' ? s.dialog.slot : 0;
  const [left, setLeft] = useState(initialLeft && names.includes(initialLeft) ? initialLeft : currentName(s, type, slot));
  const [right, setRight] = useState(initialRight ?? names.find((n) => n !== left) ?? left);
  const ready = useTabLoaded(type);
  const config = (name: string) => (name === currentName(s, type, slot) ? effectiveConfig(s, type, slot) : resolvePreset(s, type, name).config);
  const rows = useMemo(() => (ready ? diffRows(type, config(left), config(right)) : []), [ready, type, left, right]);
  const close = () => setState({ presetUx: null });
  const pick = (value: string, set: (v: string) => void, label: string) => (
    <select className="combo" aria-label={label} value={value} onChange={(e) => set(e.target.value)}>
      {names.map((n) => <option key={n} value={n}>{n}{n === currentName(s, type, slot) && Object.keys(presetEdits(s, type, slot)).length ? ' (modified)' : ''}</option>)}
    </select>
  );
  return (
    <Modal className="np-dialog compare-dialog" label="Compare presets" onClose={close} backdropClose>
      <ModalTitle title="Compare presets" onClose={close} />
      <div className="np-dialog-body">
        <div className="compare-pickers">
          <select className="combo" aria-label="Preset type" value={type} onChange={(e) => {
            const t = e.target.value as PresetType;
            const list = presetNames(getState(), t);
            setType(t); setLeft(currentName(getState(), t, 0)); setRight(list.find((n) => n !== currentName(getState(), t, 0)) ?? list[0] ?? '');
          }}>
            {(['machine', 'filament', 'process'] as const).map((t) => <option key={t} value={t}>{TYPE_NAME[t]}</option>)}
          </select>
          {pick(left, setLeft, 'Left preset')}
          <button type="button" className="btn" aria-label="Swap presets" onClick={() => { setLeft(right); setRight(left); }}>Swap</button>
          {pick(right, setRight, 'Right preset')}
        </div>
        {!ready ? <div role="status">Loading settings…</div>
          : rows.length ? <DiffTable rows={rows} left={left} right={right} />
          : <div className="hint-text">No differences</div>}
      </div>
      <div className="modal-buttons np-buttons"><button type="button" className="btn primary" data-autofocus onClick={close}>Close</button></div>
    </Modal>
  );
}
