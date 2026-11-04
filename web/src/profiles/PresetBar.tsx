import { findProfile } from './identity';
import { profileEdit } from '../flow/profile-edit';
import { useEffect, useMemo, useRef, useState } from 'react';
import type { PresetType } from '../api/types';
import { MenuButton } from '../components/Menu';
import { Popover } from 'radix-ui';
import { modeVisible, OPTIONS, splitKey, TABS, type OptionMode } from '../options';
import { effectiveMode, getState, presetEdits, resolvePreset, setState, useStore, type State } from '../state/store';
import { useMachine } from '../flow/fsm';
import { display, isMetaKey } from './format';
import { Glyph } from '../components/Glyph';
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
import { createProfileSave } from '../flow/profile-save';
import { autoKeysOf, newProfileName, newProfileNameError, saveAsNewProfile, saveToProfile, savesInPlace } from './save-to-profile';

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

const label = (c: Change) => c.key === 'nonplanar_mode' ? 'Slicing method' : OPTIONS[c.key]?.label || c.key;
const locate = (type: PresetType, key: string) => {
  const pages = TABS[TAB_OF[type]]?.pages ?? [];
  for (const [pi, page] of pages.entries()) for (const [gi, group] of page.groups.entries())
    if (group.lines.some((l) => l.options.some((k) => splitKey(k)[0] === key))) return { pi, gi, page: page.title, group: group.title };
  return { pi: 1e9, gi: 0, page: 'Other', group: '' };
};
const MODE_NAME: Partial<Record<OptionMode, string>> = { advanced: 'Advanced', develop: 'Expert', developer: 'Developer' };

function Changes({ type, list, onJump }: { type: PresetType; list: Change[]; onJump(): void }) {
  const focus = (c: Change) => {
    onJump();
    const s = getState();
    if (c.owner.object !== undefined) {
      const object = s.objects.find((o) => o.id === c.owner.object);
      if (object) setState({ processScope: 'objects', selected: { object: object.id, instance: object.instances[0]?.id ?? 0, volume: c.owner.volume, range: c.owner.range } });
      return;
    }
    const at = locate(type, c.key), rank = OPTIONS[c.key]?.mode;
    if (at.pi === 1e9) return;
    const patch: Partial<State> = {};
    if (type === 'process' && s.processScope !== 'global') patch.processScope = 'global';
    if (rank && !modeVisible(rank, effectiveMode(s))) {
      if (rank === 'developer') patch.developerMode = true; else patch.uiMode = rank;
      const prior = { uiMode: s.uiMode, developerMode: s.developerMode };
      toast(`Switched to ${MODE_NAME[rank] ?? rank} to show ${label(c)}`, { label: 'Undo', run: () => setState(prior) });
    }
    const reveal = () => setState({ settingsFocus: { type, page: at.page, key: c.key, nonce: Date.now() } });
    if (Object.keys(patch).length) { setState(patch); setTimeout(reveal, 50); } else reveal();
  };
  const rows = list.map((c) => ({ c, at: locate(type, c.key) })).sort((x, y) => x.at.pi - y.at.pi || x.at.gi - y.at.gi);
  const scoped = new Set(list.map(where)).size > 1, nested = list.length > 1;
  let page = '', group = '';
  return <div className="pf-change-scroll" role="list" data-nested={nested || undefined}>{rows.flatMap(({ c, at }) => {
    const out = [];
    if (nested && at.page !== page) { page = at.page; group = ''; out.push(<div key={`p:${page}`} className="pf-change-group" role="presentation">{page}</div>); }
    if (nested && at.group && at.group !== group) { group = at.group; out.push(<div key={`g:${page}:${group}`} className="pf-change-sub" role="presentation">{group}</div>); }
    const name = label(c), was = display(c.key, c.was), now = display(c.key, c.now), scope = scoped ? where(c) : '';
    out.push(<div key={`${c.scope}:${c.owner.object ?? ''}:${c.owner.volume ?? ''}:${c.owner.range ?? ''}:${c.key}`} className="pf-change" role="listitem">
      <button type="button" className="pf-change-line" aria-label={`${name}${scope ? `, ${scope}` : ''}: ${was} to ${now}`} data-full-text={`${name}${scope ? ` (${scope})` : ''}: ${was} → ${now}`} onClick={() => focus(c)}>
        <span className="pf-change-name">{name}</span><span className="pf-change-was">{was}</span><span className="pf-change-to" aria-hidden>→</span><span className="pf-change-now">{now}{scope && <span className="pf-change-scope"> · {scope}</span>}</span>
      </button>
      <button type="button" className="reset-icon" aria-label={`Reset ${name}`} onClick={() => revert(c)}><Glyph name="undo" size={14}/></button>
    </div>);
    return out;
  })}</div>;
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

function ChangesPop({type,slot,list,project,save}: {type:PresetType;slot:number;list:Change[];project:number;save():void}) {
  const [open, setOpen] = useState(false);
  const jumped = useRef(false);
  useEffect(() => {
    const take = () => {
      if (pending && pending.type === type && pending.slot === slot && Date.now() - pending.at < 3000) { pending = null; setOpen(true); }
    };
    take();
    const on = () => take();
    window.addEventListener(REVIEW, on);
    return () => window.removeEventListener(REVIEW, on);
  }, [type, slot]);
  useEffect(() => { if (!list.length) setOpen(false); }, [list.length]);
  if (!list.length) return null;
  const label = `${list.length} change${list.length === 1 ? '' : 's'}`;
  return <Popover.Root open={open} onOpenChange={setOpen}><Popover.Trigger asChild><button type="button" className="link pf-changes-trigger" aria-expanded={open}>{label}</button></Popover.Trigger><Popover.Portal><Popover.Content className="orca-menu pf-changes-pop" role="dialog" aria-label="Changes" collisionPadding={12} side="bottom" sideOffset={4} align="start" onCloseAutoFocus={(e) => {if (jumped.current) {jumped.current = false; e.preventDefault();}}}>
    <Changes type={type} list={list} onJump={() => {jumped.current = true; setOpen(false);}}/>
    <SaveChanges type={type} slot={slot} project={project} list={list} review={() => {setOpen(false);save();}} revertAll={() => undoable('Revert all changes', () => list.forEach(revert))}/>
  </Popover.Content></Popover.Portal></Popover.Root>;

}

function SaveChanges({type, slot, project, list, review, revertAll}: {type: PresetType; slot: number; project: number; list: Change[]; review(): void; revertAll(): void}) {
  const m = useMemo(createProfileSave, []), snap = useMachine(m);
  const auto = useStore((s) => [...autoKeysOf(s, type, slot)].join(',')), autoSet = new Set(auto ? auto.split(',') : []);
  const projectList = list.filter((c) => c.scope === 'project' && c.owner.plate === undefined);
  const plain = projectList.filter((c) => !autoSet.has(c.key)).length, scaled = projectList.length - plain;
  const owned = useStore((s) => savesInPlace(s, type, slot)), inPlace = owned && plain > 0;
  const suggested = useStore((s) => newProfileName(s, type, slot));
  const [name, setName] = useState(suggested);
  useEffect(() => setName(suggested), [suggested]);
  const nameError = useStore((s) => newProfileNameError(s, type, name));
  const saving = snap.value === 'saving', error = snap.value === 'failed' ? snap.context.error : null;
  const asNew = !owned || scaled > 0;
  const run = (inPlace: boolean) => {
    if (!m.can({type: 'SAVE'})) return;
    m.send({type: 'SAVE'});
    const request = m.get().context.request;
    (inPlace ? saveToProfile(type, slot) : saveAsNewProfile(type, slot, name))
      .then(() => m.send({type: 'SAVED', request}), (e) => m.send({type: 'FAIL', request, error: e instanceof Error ? e.message : String(e)}));
  };
  return <>
    {project > 0 && asNew && <label className="pf-save-name"><span>Name</span><input className="text" value={name} disabled={saving} aria-invalid={!!nameError}
      onChange={(e) => {setName(e.target.value); if (error) m.send({type: 'DISMISS'});}} onKeyDown={(e) => {if (e.key === 'Enter' && !nameError) run(false);}}/></label>}
    {error && <p className="pf-save-error" role="alert">{error}</p>}
    <div className="pf-changes-foot"><button type="button" className="btn-ghost" disabled={saving} onClick={revertAll}>Revert all</button>
      {project > 0 && <><button type="button" className="link pf-review" disabled={saving} onClick={review}>Open in Profiles</button>
        {asNew && <button type="button" className={inPlace ? 'btn' : 'btn primary'} disabled={saving || !!nameError} aria-busy={saving} onClick={() => run(false)}>{saving && !inPlace && <span className="pf-spin" aria-hidden/>}{error && !inPlace ? 'Retry' : 'Save as new profile'}</button>}
        {inPlace && <button type="button" className="btn primary" disabled={saving} aria-busy={saving} onClick={() => run(true)}>{saving && <span className="pf-spin" aria-hidden/>}{error ? 'Retry' : 'Save to profile'}</button>}</>}</div>
  </>;
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
  const {list,project,save}=usePresetActions(type,slot);
  return <ChangesPop type={type} slot={slot} list={list} project={project} save={save}/>;
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
