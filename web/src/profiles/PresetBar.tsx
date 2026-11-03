import { findProfile } from './identity';
import { profileEdit } from '../flow/profile-edit';
import { useEffect, useState } from 'react';
import type { PresetType } from '../api/types';
import { MenuButton } from '../components/Menu';
import { Popover } from 'radix-ui';
import { OPTIONS, splitKey, TABS } from '../options';
import { getState, presetEdits, resolvePreset, setState, useStore } from '../state/store';
import { useMachine } from '../flow/fsm';
import { display, isMetaKey } from './format';
import { undoable } from '../state/history';
import { PresetPicker } from './Pickers';
import { ProcessPresetPicker } from '../components/ProcessPresetPicker';
import '../components/process-settings.css';
import { revert, revertAll } from './stack';
import { resolveProfileConflict, selectProfileConflicts, type ProfileConflict } from './conflicts';
import type { SettingChange as Change } from '../state/changes';
import { useChanges } from '../state/useChanges';
export { useChanges } from '../state/useChanges';
import { reviewProjectProfileChanges } from './project-draft';
import { toast } from '../state/store';
import { editing } from './editing';

async function exportCurrent(type: PresetType, slot: number) {
  const s = getState();
  const name = (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
  const { base, user } = resolvePreset(s, type, name);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const { exportPresetJson } = await import('../preset-io');
  exportPresetJson({ name: user?.name ?? base?.name ?? 'Profile', type, inherits: base?.name ?? name, overrides: { ...user?.overrides, ...project, ...presetEdits(s, type, slot) } });
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
  const label=(c:Change)=>c.key === 'nonplanar_mode' ? 'Slicing method' : OPTIONS[c.key]?.label || c.key;
  return <div className="pf-change-scroll"><table className="pf-change-table"><colgroup><col className="change-setting"/><col className="change-value"/><col className="change-value"/><col className="change-scope"/><col className="change-reset"/></colgroup><thead><tr><th>Setting</th><th>Previous</th><th>Current</th><th>Scope</th><th><span className="field-label-hidden">Reset</span></th></tr></thead><tbody>
    {shown.map(c=><tr key={`${c.scope}:${c.owner.object ?? ''}:${c.owner.volume ?? ''}:${c.owner.range ?? ''}:${c.key}`}><td data-label="Setting"><button type="button" className="pf-name" onClick={()=>focus(c)} title={c.key}>{label(c)}</button></td><td data-label="Previous">{display(c.key,c.was)}</td><td data-label="Current">{display(c.key,c.now)}</td><td data-label="Scope">{where(c)}</td><td><button type="button" className="btn-ghost" aria-label={`Reset ${label(c)}`} onClick={()=>revert(c)}>Reset</button></td></tr>)}
  </tbody></table>{list.length > shown.length && <button type="button" className="btn-ghost" onClick={()=>setAll(true)}>Show {list.length - shown.length} more changes</button>}</div>;

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

function ChangesPop({type,slot,list,project,state}: {type:PresetType;slot:number;list:Change[];project:number;state:string}) {
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
  const label = state === 'saving' ? 'Saving…' : state === 'saved' && !list.length ? 'Saved to profile' : `Changes · ${list.length}`;
  const resetAll = async () => {
    const {confirmDialog}=await import('../components/Ask');
    if(await confirmDialog('Revert all changes',`Reset all ${list.length} listed changes, including object and part overrides?`,['Revert all','Cancel'])) undoable('Revert all listed changes',()=>list.forEach(revert));
  };
  return <Popover.Root open={open} onOpenChange={setOpen}><Popover.Trigger asChild><button type="button" className="btn pf-changes-trigger" aria-label={label} aria-expanded={open}>{label}</button></Popover.Trigger><Popover.Portal><Popover.Content className="orca-menu pf-changes-pop" role="dialog" aria-label="Project changes" collisionPadding={12} sideOffset={6} align="end">
    <div className="pf-changes-title">Project changes</div><Changes type={type} list={list}/>
    {!!list.length && <div className="pf-changes-foot"><button type="button" className="btn" onClick={()=>void import('../flow/project').then(m=>m.flushProject()).catch(error=>import('../state/store').then(m=>m.toast(`Project save failed: ${error instanceof Error ? error.message : String(error)}`)))}>Save project</button>
      {project > 0 && <button type="button" className="btn" onClick={()=>{try {reviewProjectProfileChanges(type,slot);setOpen(false);}catch(error){toast(error instanceof Error ? error.message : String(error));}}}>Review in profile editor…</button>}
      <button type="button" className="btn-ghost" onClick={()=>void resetAll()}>Revert all</button></div>}
  </Popover.Content></Popover.Portal></Popover.Root>;

}

function usePresetActions(type: PresetType, slot: number) {
  const list = useChanges(type, slot).filter((c) => !isMetaKey(c.key));
  const project = list.filter((c) => c.scope === 'project' && c.owner.plate === undefined).length;
  const m = editing(type, slot);
  const state = useMachine(m, (x) => x.value);
  useEffect(() => m.send({ type: 'EDITS', n: project }), [m, project]);
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const isUser = useStore((s) => !!findProfile(s,type,presetName ?? ''));

  const onDelete = () => {
    const u = findProfile(getState(),type,presetName ?? '');
    if (u) void import('./PresetsWindow').then((m) => m.removePreset(type, u));
  };
  const save=()=>{try {reviewProjectProfileChanges(type,slot);}catch(error){toast(error instanceof Error ? error.message : String(error));}};
  return { list, project, state, presetName, isUser, onDelete, save };
}

export function PresetActions({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  const { project, presetName, isUser, onDelete, save } = usePresetActions(type, slot);
  return (
    <PresetMenu label={`${TYPE_NAME[type]} preset actions`} items={[
      { icon: 'save', label: 'Review project changes in profile editor', run: save, disabled: !project && isUser },

      { icon: 'undo', label: 'Revert all', run: () => revertAll(type, slot), disabled: !project },
      { icon: 'compare', label: 'Compare', run: () => setState({ presetUx: { kind: 'compare', type, left: presetName } }) },
      { icon: 'menu_edit_preset', label: 'Edit profile', run: () => setState({ presetUx: { kind: 'presets', type, name: presetName, edit: true } }) },
      { icon: 'design_export', label: 'Export', run: () => void exportCurrent(type, slot) },
      { icon: 'delete', label: 'Delete', run: onDelete, disabled: !isUser },
    ]} />
  );
}

export function ChangesMark({ type, slot = 0 }: { type: PresetType; slot?: number }) {
  const {list,project,state}=usePresetActions(type,slot);
  return <ChangesPop type={type} slot={slot} list={list} project={project} state={state} />;
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
        <div className="profile-choice-row">{type === 'process' ? <ProcessPresetPicker/> : <PresetPicker type={type} slot={slot}/>}</div>
        <div className="profile-action-row"><button type="button" className="btn profile-edit-action" onClick={() => setState({presetUx: {kind: 'presets', type, name: presetName, edit: true}})}>Edit</button>
        <ChangesMark type={type} slot={slot}/><PresetActions type={type} slot={slot}/></div>
      </div>
      {presetName && <ConflictLine type={type} name={presetName} slot={slot} />}
    </>
  );
}
