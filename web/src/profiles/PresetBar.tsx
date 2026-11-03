import { profileEdit } from '../flow/profile-edit';
import { resetToBase } from './catalog';
import { useEffect, useState } from 'react';
import type { PresetType } from '../api/types';
import { MenuButton } from '../components/Menu';
import { Popover } from 'radix-ui';
import { OPTIONS, splitKey, TABS } from '../options';
import { getState, presetEdits, resolvePreset, setState, useStore } from '../state/store';
import { useMachine } from '../flow/fsm';
import { display, isMetaKey } from './format';
import { Icon } from '../components/Icon';
import { PresetPicker } from './Pickers';
import { ProcessPresetPicker } from '../components/ProcessPresetPicker';
import '../components/process-settings.css';
import { revert, revertAll } from './stack';
import { resolveProfileConflict, selectProfileConflicts, type ProfileConflict } from './conflicts';
import type { SettingChange as Change } from '../state/changes';
import { useChanges } from '../state/useChanges';
export { useChanges } from '../state/useChanges';
import { editing } from './editing';

async function exportCurrent(type: PresetType, slot: number) {
  const s = getState();
  const name = (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
  const { base, user } = resolvePreset(s, type, name);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const { exportPresetJson } = await import('../preset-io');
  exportPresetJson({ name, type, inherits: base?.name ?? name, overrides: { ...user?.overrides, ...project, ...presetEdits(s, type, slot) } });
}

interface MenuItem { icon: string; label: string; run(): void; disabled?: boolean }

function PresetMenu({ label, items }: { label: string; items: MenuItem[] }) {
  return <MenuButton className="icon-btn preset-more" label={label} items={items.map(it => ({label: it.label, action: it.run, disabled: it.disabled}))}>⋯</MenuButton>;

}

const TYPE_NAME: Record<PresetType, string> = { process: 'Process', filament: 'Filament', machine: 'Printer' };
const TAB_OF = { process: 'process', filament: 'filament', machine: 'printer' } as const;

const where = (c: Change) => c.scope === 'project' ? c.owner.name : `${c.scope === 'object' ? 'Object' : c.scope === 'part' ? 'Part' : 'Range'}: ${c.owner.name}`;

function Changes({ type, list }: { type: PresetType; list: Change[] }) {
  const [all, setAll] = useState(false);
  const shown = all ? list : list.slice(0, 6);
  const focus = (c: Change) => {
    if (c.owner.object !== undefined) {
      const object = getState().objects.find((o) => o.id === c.owner.object);
      if (object) setState({ processScope: 'objects', selected: { object: object.id, instance: object.instances[0]?.id ?? 0, volume: c.owner.volume, range: c.owner.range } });
      return;
    }
    const page = TABS[TAB_OF[type]]?.pages.find((p) => p.groups.some((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] === c.key))));
    if (page) setState({ settingsFocus: { type, page: page.title, key: c.key, nonce: Date.now() } });
  };
  return (
    <table className="pf-table pf-changes">
      <thead><tr><th>Setting</th><th className="n">Was</th><th className="n">Now</th><th>Where</th><th aria-label="Revert" /></tr></thead>
      <tbody>
        {shown.map((c) => (
          <tr key={`${c.scope}:${c.owner.object ?? ''}:${c.owner.volume ?? ''}:${c.owner.range ?? ''}:${c.key}`}>
            <td><button type="button" className="pf-name" onClick={() => focus(c)} title={c.key}>{OPTIONS[c.key]?.label || c.key}</button></td>
            <td className="n">{display(c.key, c.was)}</td>
            <td className="n">{display(c.key, c.now)}</td>
            <td>{where(c)}</td>
            <td><button type="button" className="icon-btn" aria-label={`Revert ${OPTIONS[c.key]?.label || c.key}`} title="Revert" onClick={() => revert(c)}><Icon name="undo" size={14} /></button></td>
          </tr>
        ))}
        {list.length > shown.length && (
          <tr><td colSpan={5}><button type="button" className="pf-name" onClick={() => setAll(true)}>{list.length - shown.length} more</button></td></tr>
        )}
      </tbody>
    </table>
  );
}

const REVIEW = 'pf-review';
let pending: { type: PresetType; slot: number; at: number } | null = null;

export function reviewChanges(type: PresetType, slot = 0) {
  pending = { type, slot, at: Date.now() };
  if (type === 'process') {
    if (getState().processScope !== 'global') setState({ processScope: 'global' });
  } else {
    const d = getState().dialog;
    if (d?.type !== type || d.slot !== slot) setState({ dialog: { type, slot } });
  }
  window.dispatchEvent(new CustomEvent(REVIEW, { detail: { type, slot } }));
}

function ChangesPop({ type, slot, list, project, state, isUser, save, saveAs }: { type: PresetType; slot: number; list: Change[]; project: number; state: string; isUser: boolean; save(): void; saveAs(): void }) {
  const [open, setOpen] = useState(false);
  useEffect(() => {
    const take = () => {
      if (pending && pending.type === type && pending.slot === slot && Date.now() - pending.at < 3000) { pending = null; setOpen(true); }
    };
    take();
    const on = () => take();
    window.addEventListener(REVIEW, on);
    return () => window.removeEventListener(REVIEW, on);
  }, [type, slot]);
  useEffect(() => { if (!list.length && state !== 'saved') setOpen(false); }, [list.length, state]);
  if (!list.length && state !== 'saved') return null;
  const label = state === 'saving' ? 'Saving' : state === 'saved' ? 'Saved' : `${list.length} changes`;
  return <Popover.Root open={open} onOpenChange={setOpen}><Popover.Trigger asChild><button type="button" className="pf-mark" aria-label={label} title={label}>⁎</button></Popover.Trigger><Popover.Portal><Popover.Content className="orca-menu pf-changes-pop" aria-label="Changes" collisionPadding={8} sideOffset={4}>
    <Changes type={type} list={list} /><div className="pf-changes-foot">{project > 0 && state !== 'saving' && <><button type="button" className="btn" onClick={isUser ? save : saveAs}>{isUser ? 'Save' : 'Save as new'}</button><button type="button" className="btn" onClick={() => revertAll(type, slot)}>Revert all</button></>}</div>
  </Popover.Content></Popover.Portal></Popover.Root>;

}

function usePresetActions(type: PresetType, slot: number) {
  const list = useChanges(type, slot).filter((c) => !isMetaKey(c.key));
  const project = list.filter((c) => c.scope === 'project' && c.owner.plate === undefined).length;
  const m = editing(type, slot);
  const state = useMachine(m, (x) => x.value);
  useEffect(() => m.send({ type: 'EDITS', n: project }), [m, project]);
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const isUser = useStore((s) => s.userPresets.some((u) => u.type === type && u.name === presetName));

  const onDelete = () => {
    const u = getState().userPresets.find((x) => x.type === type && x.name === presetName);
    if (u) void import('./PresetsWindow').then((m) => m.removePreset(type, u));
  };
  const save = () => {
    m.send({ type: 'SAVE' });
    if (m.get().value === 'naming') setState({ presetUx: { kind: 'save', type, slot } });
  };
  const saveAs = () => { m.send({ type: 'SAVE_AS' }); setState({ presetUx: { kind: 'save', type, slot } }); };
  return { list, project, state, presetName, isUser, onDelete, save, saveAs };
}

export function PresetActions({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  const { project, presetName, isUser, onDelete, save, saveAs } = usePresetActions(type, slot);
  return (
    <PresetMenu label={`${TYPE_NAME[type]} preset actions`} items={[
      { icon: 'save', label: isUser ? 'Save' : 'Save as new', run: isUser ? save : saveAs, disabled: !project && isUser },
      ...(isUser ? [{ icon: 'save', label: 'Save as new', run: saveAs }] : []),
      { icon: 'undo', label: 'Revert all', run: () => revertAll(type, slot), disabled: !project },
      { icon: 'undo', label: 'Reset to base', run: () => resetToBase(type, presetName ?? ''), disabled: !isUser },
      { icon: 'compare', label: 'Compare', run: () => setState({ presetUx: { kind: 'compare', type, left: presetName } }) },
      { icon: 'menu_edit_preset', label: 'Edit profile', run: () => setState({ presetUx: { kind: 'presets', type, name: presetName, edit: true } }) },
      { icon: 'design_export', label: 'Export', run: () => void exportCurrent(type, slot) },
      { icon: 'delete', label: 'Delete', run: onDelete, disabled: !isUser },
    ]} />
  );
}

export function ChangesMark({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  const { list, project, state, isUser, save, saveAs } = usePresetActions(type, slot);
  return <ChangesPop type={type} slot={slot} list={list} project={project} state={state} isUser={isUser} save={save} saveAs={saveAs} />;
}

const EMPTY: ProfileConflict[] = [];
export function ConflictLine({ type, name, slot = 0 }: { type: PresetType; name: string; slot?: number }) {
  const s = useStore((s) => s);
  const lifecycle = useMachine(profileEdit(type, slot));
  const current = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
  const rows = name === current ? lifecycle.context.conflicts : name ? selectProfileConflicts(s, type, name) : EMPTY;
  if (!rows.length) return null;
  const c = rows[0];
  return (
    <div className="pf-conflict" role="status">
      <span className="pf-conflict-text">{OPTIONS[c.key]?.label || c.key}{rows.length > 1 ? ` +${rows.length - 1}` : ''}</span>
      <button type="button" className="pf-name" onClick={() => void resolveProfileConflict(type, name, c.key, 'mine')}>Keep mine</button>
      <button type="button" className="pf-name" onClick={() => void resolveProfileConflict(type, name, c.key, 'theirs')}>Take new</button>
    </div>
  );
}

export function PresetBar({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  const { presetName } = usePresetActions(type, slot);
  return (
    <>
      <div className="tab-preset-row">
        {type === 'process' ? <ProcessPresetPicker /> : <PresetPicker type={type} slot={slot} />}
        <button type="button" className="btn profile-edit-action" onClick={() => setState({presetUx: {kind: 'presets', type, name: presetName, edit: true}})}>Edit</button>
        <ChangesMark type={type} slot={slot} /><PresetActions type={type} slot={slot} />
      </div>
      {presetName && <ConflictLine type={type} name={presetName} slot={slot} />}
    </>
  );
}
