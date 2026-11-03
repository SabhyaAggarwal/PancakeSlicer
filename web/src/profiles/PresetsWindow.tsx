import { ProfileEditor, type ProfileDraft } from './ProfileEditor';
import { profileSummary } from './metadata';
import { Check } from '../components/Check';
import { Dropdown } from '../components/Dropdown';
import { SectionRule, Segmented } from '../components/kit';
import './presets-window.css';
import { useCallback, useMemo, useState } from 'react';
import type { ConfigValue, PresetType, UserPreset } from '../api/types';
import { accessSnapshot } from '../api/access';
import { OPTIONS } from '../options';
import { compatible, getState, resolvePreset, setState, toast, useStore, type State } from '../state/store';
import { Modal, ModalTitle } from '../components/Modal';
import { confirmDialog } from '../components/Ask';
import { diffRows, useTabLoaded } from '../components/PresetDialogs';
import { display, presetLabel } from './format';
import { changeCount, takeValue } from './stack';
import { requestPreset } from '../components/PresetCombo';
import { createProfileFromBase } from './catalog';
import { listCatalog } from './pickerSource';

const TYPES: [PresetType, string][] = [['machine', 'Printer'], ['filament', 'Filament'], ['process', 'Print settings']];
const shortName = presetLabel;

interface Item { name: string; mine: boolean; parent?: string; changed: number; slot?: number; user?: UserPreset; note: string }

const slotsOf = (s: State, type: PresetType, name: string) =>
  type === 'machine' ? (s.printer === name ? [0] : []) : type === 'process' ? (s.process === name ? [0] : [])
    : s.filamentSlots.flatMap((f, i) => (f === name ? [i] : []));

function noteOf(s: State, type: PresetType, name: string) {
  const c = resolvePreset(s, type, name).config;
  return profileSummary(type, c);
}

function items(s: State, type: PresetType): Item[] {
  const system = type === 'machine' ? s.machines : compatible(s, type);
  const mine = s.userPresets.filter((u) => u.type === type);
  const item = (name: string, isMine: boolean, parent?: string, user?: UserPreset): Item => {
    const slots = slotsOf(s, type, name);
    return { name, mine: isMine, parent, user, slot: slots[0], note: noteOf(s, type, name), changed: slots.reduce((n, i) => n + changeCount(s, type, i), 0) };
  };
  const inUse = (i: Item) => (i.slot === undefined ? 1 : 0);
  const order = (a: Item, b: Item) => inUse(a) - inUse(b) || (a.name < b.name ? -1 : 1);
  return [
    ...mine.map((u) => item(u.name, true, u.inherits, u)).sort(order),
    ...system.filter((p) => slotsOf(s, type, p.name).length > 0).map((p) => item(p.name, false)).sort(order),
  ];
}

const failed = (action: string, e: unknown) => {
  const message = e instanceof Error ? e.message : String(e);
  if (accessSnapshot()?.message !== message) toast(`${action} failed: ${message}`);
};

async function store(p: UserPreset, from?: string) {
  const s = getState();
  await s.api!.saveUserPreset(p);
  if (from && from !== p.name) await s.api!.deleteUserPreset(p.type, from);
  setState((st) => {
    const userPresets = [...st.userPresets.filter((u) => !(u.type === p.type && (u.name === p.name || u.name === from))), p];
    const move = (n: string) => (from && n === from ? p.name : n);
    const patch: Partial<State> = { userPresets };
    if (p.type === 'machine') patch.printer = move(st.printer);
    else if (p.type === 'process') patch.process = move(st.process);
    else {
      patch.filamentSlots = st.filamentSlots.map(move);
      if (from && st.filamentEdits[from]) {
        const filamentEdits = { ...st.filamentEdits, [p.name]: st.filamentEdits[from] };
        delete filamentEdits[from];
        patch.filamentEdits = filamentEdits;
      }
    }
    return patch;
  });
}

export async function removePreset(type: PresetType, u: UserPreset) {
  const s = getState();
  const slots = slotsOf(s, type, u.name);
  if (slots.length || s.userPresets.some(p => p !== u && p.type === type && (p.parent === u.name || p.inherits === u.name))) return void toast('This profile is in use. Choose another profile before deleting it.');
  try { await s.api!.deleteUserPreset(type, u.name); } catch (e) { return failed('Delete', e); }
  setState((st) => {
    const patch: Partial<State> = { userPresets: st.userPresets.filter((x) => !(x.type === type && x.name === u.name)) };
    if (type === 'machine' && st.printer === u.name) patch.printer = u.inherits;
    else if (type === 'process' && st.process === u.name) patch.process = u.inherits;
    else if (type === 'filament') patch.filamentSlots = st.filamentSlots.map((f) => (f === u.name ? u.inherits : f));
    return patch;
  });
  toast('Deleted', { label: 'Undo', run: () => void store(u).then(() => setState((st) => (
    type === 'machine' ? { printer: slots.length ? u.name : st.printer } : type === 'process' ? { process: slots.length ? u.name : st.process }
      : { filamentSlots: st.filamentSlots.map((f, i) => (slots.includes(i) ? u.name : f)) })), (e) => failed('Undo', e)) });
}

function AddProfile({ type, done, close }: { type: PresetType; done(name: string): void; close(): void }) {
  const [search, setSearch] = useState('');
  useStore((st) => st.printer);
  const rows = listCatalog(type).filter((r) => `${r.name} ${r.vendor}`.toLowerCase().includes(search.toLowerCase()));
  const add = (base: string) => {
    try { const name = createProfileFromBase(type, base); done(name); } catch (e) { failed('Add', e); }
  };
  return (
    <fieldset className="pf-box pf-details">
      <legend>Add from catalog</legend>
      <input className="text" autoFocus aria-label="Search" placeholder="Search" value={search} onChange={(e) => setSearch(e.target.value)} />
      <div className="pf-scroll pf-list" role="listbox" aria-label="Catalog">
        {rows.map((r) => (
          <div key={r.name} role="option" aria-selected={false} aria-disabled={!!r.reason} title={[r.name, r.vendor, r.reason].filter(Boolean).join(' · ')}
            className={`pf-item${r.reason ? ' is-dim' : ''}`} tabIndex={0} onClick={() => !r.reason && add(r.name)}
            onKeyDown={(e) => e.key === 'Enter' && !r.reason && add(r.name)}>
            <span className="pf-item-name" title={r.name}>{presetLabel(r.name)}</span>
            <span className="pf-dim pf-item-note">{[r.vendor, noteOf(getState(), type, r.name)].filter(Boolean).join(' · ')}</span>
          </div>
        ))}
        {!rows.length && <div className="pf-dim">No results</div>}
      </div>
      <div className="pf-actions"><button type="button" className="btn" onClick={close}>Cancel</button></div>
    </fieldset>
  );
}

function Details({type, item, onCompare, draft, update}: {type: PresetType; item: Item; onCompare(): void; draft?: ProfileDraft; update(key: string, draft: ProfileDraft | null): void}) {
  const exportOne = async () => { const {exportPresetJson} = await import('../preset-io'); exportPresetJson(item.user ?? {name: item.name, type, inherits: item.name, overrides: {}}); };
  return <ProfileEditor type={type} name={item.name} draft={draft} update={update} actions={<>
    {item.slot === undefined && <button type="button" className="btn" disabled={!!draft} title={draft ? "Save or cancel profile edits before using it" : "Select this profile for the project"} onClick={() => requestPreset(type, 0, item.name)}>{type === 'filament' ? 'Use in slot 1' : 'Use in project'}</button>}
    {item.slot !== undefined && <span className="pf-in-use">✓ In use</span>}
    <button type="button" className="btn" onClick={onCompare}>Compare</button>
    <button type="button" className="btn" onClick={() => void exportOne()}>Export</button>
    {item.user && <button type="button" className="btn-ghost pf-delete" disabled={item.slot !== undefined} title={item.slot !== undefined ? 'This profile is in use by the current project' : 'Delete profile'} onClick={() => void confirmDialog('Delete profile', `Delete “${item.name}” from your profile library?`, ['Delete', 'Cancel']).then(ok => {if(ok && item.user) void removePreset(type, item.user);})}>Delete</button>}
  </>}/>;
}

function configOf(s: State, type: PresetType, name: string) {
  const slot = slotsOf(s, type, name)[0];
  if (slot === undefined) return resolvePreset(s, type, name).config;
  const { config } = resolvePreset(s, type, name);
  const edits = type === 'filament' ? s.filamentEdits[name] : s.dirty[type];
  return { ...config, ...edits };
}

function Compare({ type, left, right, names, setRight, close }: { type: PresetType; left: string; right: string; names: string[]; setRight(n: string): void; close(): void }) {
  const ready = useTabLoaded(type);
  const [only, setOnly] = useState(true);
  useStore((s) => s.dirty);
  useStore((s) => s.filamentEdits);
  const s = getState();
  const leftSlot = slotsOf(s, type, left)[0];
  const rows = useMemo(() => (ready ? diffRows(type, configOf(s, type, left), configOf(s, type, right), !only) : []), [ready, type, left, right, only, s.dirty, s.filamentEdits]);
  const changed = rows.filter((r) => r.differs).length;
  let group = '';
  return (
    <fieldset className="pf-box pf-compare">
      <legend>Compare</legend>
      <div className="pf-compare-bar">
        <span>{shortName(left)}</span>
        <span aria-hidden>↔</span>
        <Dropdown className="combo" aria-label="Compare with" value={right} onValueChange={setRight} options={[...names.filter((n) => n !== left).map((n) => ({ value: n, label: shortName(n) }))]} />
        <label className="row"><Check checked={only} onCheckedChange={(next) => setOnly(next)} />Only differences</label>
        <span className="pf-dim">{changed}</span>
        <span className="spacer" />
        <button type="button" className="pf-link" onClick={close}>Close</button>
      </div>
      <div className="pf-scroll pf-diff">
        {!ready ? <div role="status">Loading…</div> : !rows.length ? <div className="pf-dim">No differences</div> : (
          <table className="pf-table">
            <thead><tr><th>Setting</th><th className="n">{shortName(left)}</th><th className="n">{shortName(right)}</th>{leftSlot !== undefined && <th aria-label="Use" />}</tr></thead>
            <tbody>{rows.flatMap((r) => {
              const out = [];
              if (r.group !== group) { group = r.group; out.push(<tr key={`g:${group}`} className="pf-group"><td colSpan={4}>{group}</td></tr>); }
              out.push(
                <tr key={r.key} className={r.differs ? 'is-diff' : undefined} title={r.key}>
                  <td>{r.label}</td>
                  <td className="n">{display(r.key, r.left ?? (OPTIONS[r.key]?.default as ConfigValue))}</td>
                  <td className="n">{display(r.key, r.right ?? (OPTIONS[r.key]?.default as ConfigValue))}</td>
                  {leftSlot !== undefined && <td>{r.differs && <button type="button" className="pf-link" onClick={() => takeValue(type, r.key, (r.right ?? OPTIONS[r.key]?.default) as ConfigValue, leftSlot)}>← Use</button>}</td>}
                </tr>,
              );
              return out;
            })}</tbody>
          </table>
        )}
      </div>
    </fieldset>
  );
}

export default function PresetsWindow({ type: initialType, name, compare }: { type: PresetType; name?: string; compare?: string }) {
  const [type, setType] = useState(initialType);
  const [drafts, setDrafts] = useState<Record<string, ProfileDraft>>({});
  const updateDraft = useCallback((key: string, draft: ProfileDraft | null) => setDrafts(prior => {const next = {...prior}; if(draft) next[key] = draft; else delete next[key]; return next;}), []);
  const [search, setSearch] = useState('');
  const s = getState();
  useStore((st) => st.userPresets);
  useStore((st) => st.dirty);
  useStore((st) => st.filamentEdits);
  useStore((st) => st.printer + st.process + st.filamentSlots.join('\n'));
  const list = items(getState(), type);
  const current = (t: PresetType) => (t === 'machine' ? s.printer : t === 'process' ? s.process : s.filamentSlots[0]) ?? '';
  const [sel, setSel] = useState(name ?? current(initialType));
  const item = list.find((i) => i.name === sel) ?? list[0];
  const names = list.map((i) => i.name);
  const partner = (i: Item) => (i.parent && names.includes(i.parent) ? i.parent : names.find((n) => n !== i.name) ?? null);
  const [adding, setAdding] = useState(false);
  const [other, setOther] = useState<string | null>(() => (compare === undefined ? null : compare || (item ? partner(item) : null)));
  const leave = async (action: () => void) => {if(Object.keys(drafts).length && !await confirmDialog('Unsaved profile edits', 'Discard unsaved profile edits?', ['Discard', 'Keep editing'])) return; action();};
  const close = () => void leave(() => setState({presetUx: null}));
  const pickType = (t: PresetType) => { setAdding(false); setType(t); setSearch(''); setSel(current(t)); setOther(null); };
  return (
    <Modal className="np-dialog pf-window" label="Presets" onClose={close} backdropClose>
      <ModalTitle title="Presets" onClose={close} />
      <div className="np-dialog-body">
        <div className="pf-tabs">
          <Segmented label="Profile type" value={type} options={TYPES.map(([id, label]) => ({ id, label }))} onChange={(id) => pickType(id as PresetType)} />
          <span className="spacer" />
          <button type="button" className="btn" onClick={() => setAdding(true)}>Add from catalog</button>
          <button type="button" className="btn" onClick={() => void leave(() => setState({presetUx: {kind: 'import', back: {kind: 'presets', type, name: item?.name}}}))}>Import Orca / Bambu</button>
          <button type="button" className="btn" onClick={() => void leave(() => setState({presetUx: {kind: 'export'}}))}>Export all</button>
        </div>
        <div className="pf-split">
          <section className="pf-my-profiles" aria-label="My profiles">
            <SectionRule title="My profiles" count={list.length} />
            <input className="text pf-profile-search" aria-label="Search profiles" placeholder="Search profiles" value={search} onChange={(e) => setSearch(e.target.value)} />
            <div className="pf-scroll pf-list" role="listbox" aria-label="Profiles">
            {list.filter((i) => i.name.toLowerCase().includes(search.toLowerCase())).map((i) => (
              <div key={i.name} role="option" title={i.name} className={`pf-item${i === item ? ' is-on' : ''}`} aria-selected={i === item} tabIndex={0} onClick={() => setSel(i.name)}
                onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && (e.preventDefault(), setSel(i.name))}
                onDoubleClick={() => { setSel(i.name); setOther(partner(i)); }}>
                <span className="pf-item-name" title={i.name}>{shortName(i.name)}</span>
                <span className="pf-dim pf-item-note">{i.note}</span>
                {i.changed > 0 && <span className="pf-dim">Changed</span>}
              </div>
            ))}
            {!list.some((i) => i.name.toLowerCase().includes(search.toLowerCase())) && <p className="pf-profile-empty">{search ? 'No matching profiles' : 'Add a profile from the catalog'}</p>}
            </div>
          </section>
          <div className="pf-detail-pane">{adding ? <AddProfile type={type} close={() => setAdding(false)} done={(n) => { setSel(n); setAdding(false); }} />
            : item && other ? <Compare type={type} left={item.name} right={other} names={names} setRight={setOther} close={() => setOther(null)} />
            : item ? <Details key={`${type}:${item.name}`} type={type} item={item} draft={drafts[JSON.stringify([type,item.name])]} update={updateDraft} onCompare={() => setOther(partner(item))} /> : <div className="pf-profile-empty">Select or add a profile</div>}</div>
        </div>
      </div>
    </Modal>
  );
}
