import { useMemo, useState, type ReactNode } from 'react';
import { createRoot } from 'react-dom/client';
import type { PresetType, UserPreset } from '../api/types';
import { Modal, ModalBody, ModalFooter, ModalTitle } from '../components/Modal';
import { Choice } from '../components/kit';
import { Dropdown } from '../components/Dropdown';
import { confirmDialog } from '../components/Ask';
import { getState, toast } from '../state/store';
import { findProfile, profileKey } from './identity';
import { listCatalog } from './catalog';
import { archiveBases, baseCandidates, baseOf, baseRoleOf, changeBase, changeBasePreview, childrenOf, createBasedOn, detachFromBase, setBaseRole, type BaseRole } from './bases';
import './bases.css';

export function BaseBadge({ profile }: { profile: UserPreset | undefined }) {
  const role = baseRoleOf(profile);
  return role ? <span className="base-badge">{role === 'base-only' ? 'Base only' : 'Base'}</span> : null;
}

function host<T>(render: (done: (value: T) => void) => ReactNode): Promise<T> {
  return new Promise((resolve) => {
    const el = document.body.appendChild(document.createElement('div')), root = createRoot(el);
    const done = (value: T) => { root.unmount(); el.remove(); resolve(value); };
    root.render(render(done));
  });
}
const failed = (error: unknown) => toast(error instanceof Error ? error.message : String(error));
function useWrite(done: () => void) {
  const [busy, setBusy] = useState(false), [error, setError] = useState<string | null>(null);
  const run = (write: () => Promise<unknown>) => { setBusy(true); setError(null); write().then(done, (e) => { setBusy(false); setError(e instanceof Error ? e.message : String(e)); }); };
  return { busy, error, run };
}
function Dialog({ title, onClose, actions, error, children }: { title: string; onClose(): void; actions: ReactNode; error?: string | null; children: ReactNode }) {
  return <Modal size="m" label={title} onClose={onClose}>
    <ModalTitle title={title} onClose={onClose}/>
    <ModalBody className="bases-dialog">{children}{error && <p className="bases-error" role="alert">{error}</p>}</ModalBody>
    <ModalFooter><button type="button" className="btn" onClick={onClose}>Cancel</button>{actions}</ModalFooter>
  </Modal>;
}

const ROLES = [{ id: 'none', label: 'Not a base' }, { id: 'base', label: 'Base' }, { id: 'base-only', label: 'Base only' }] as const;
function MakeBase({ type, profile, done }: { type: PresetType; profile: UserPreset; done(): void }) {
  const [role, setRole] = useState<string>(baseRoleOf(profile) ?? 'base'), { busy, error, run } = useWrite(done);
  const children = childrenOf(getState(), type, profileKey(profile)).length;
  return <Dialog title={`${profile.name}: base`} onClose={done} error={error}
    actions={<button type="button" className="btn-primary" disabled={busy || role === (baseRoleOf(profile) ?? 'none') || role === 'none' && children > 0}
      onClick={() => run(() => setBaseRole(type, profileKey(profile), role === 'none' ? undefined : role as BaseRole))}>Save</button>}>
    <Choice label="Base" value={role} options={ROLES} onChange={setRole}/>
    <p className="bases-hint">{role === 'base-only' ? 'Offered only to build profiles on, not in the print pickers.' : role === 'base' ? 'Offered to build profiles on and printable like any profile.' : children ? `${children} profiles are based on it.` : 'An ordinary profile.'}</p>
  </Dialog>;
}

function NewBasedOn({ type, from, done }: { type: PresetType; from?: string; done(): void }) {
  const s = getState(), bases = baseCandidates(s, type), masters = useMemo(() => listCatalog(s, type, { browseAll: true }), [s, type]);
  const options = [...bases.map((p) => ({ value: `base:${profileKey(p)}`, label: `${p.name} (my base)` })), ...masters.map((m) => ({ value: `master:${m.key}`, label: m.name }))];
  const [choice, setChoice] = useState(from ? `base:${from}` : options[0]?.value ?? ''), [name, setName] = useState(''), { busy, error, run } = useWrite(done);
  const picked = options.find((o) => o.value === choice);
  const create = () => run(async () => {
    const [kind, key] = [choice.slice(0, choice.indexOf(':')), choice.slice(choice.indexOf(':') + 1)];
    if (kind === 'base') await createBasedOn(type, key, name.trim());
    else await (await import('./catalog')).createProfileFromBase(type, key, name.trim() || undefined);
  });
  return <Dialog title="New profile based on…" onClose={done} error={error}
    actions={<button type="button" className="btn-primary" disabled={busy || !picked || choice.startsWith('base:') && !name.trim()} onClick={create}>Create</button>}>
    <label className="form-row"><span>Based on</span>
      <Dropdown aria-label="Based on" value={choice} options={options} onValueChange={setChoice}/></label>
    <label className="form-row"><span>Name</span><input className="text" aria-label="Name" value={name} onChange={(e) => setName(e.target.value)}/></label>
  </Dialog>;
}

function ChangeBase({ type, profile, done }: { type: PresetType; profile: UserPreset; done(): void }) {
  const key = profileKey(profile), current = profile.parentId ?? '', candidates = baseCandidates(getState(), type, key);
  const [parent, setParent] = useState(current), { busy, error, run } = useWrite(done);
  const changes = useMemo(() => parent === current ? [] : changeBasePreview(getState(), type, key, parent || null), [parent]);
  const options = [{ value: '', label: 'System preset (no base)' }, ...candidates.map((p) => ({ value: profileKey(p), label: p.name }))];
  return <Dialog title={`Change base of ${profile.name}`} onClose={done} error={error}
    actions={<button type="button" className="btn-primary" disabled={busy || parent === current} onClick={() => run(() => changeBase(type, key, parent || null))}>Change base</button>}>
    <label className="form-row"><span>Base</span>
      <Dropdown aria-label="Base" value={parent} options={options} onValueChange={setParent}/></label>
    {parent !== current && (changes.length
      ? <table className="bases-changes"><thead><tr><th>Setting</th><th>Now</th><th>After</th></tr></thead>
        <tbody>{changes.map((c) => <tr key={c.key}><td>{c.label}</td><td>{c.from}</td><td>{c.to}</td></tr>)}</tbody></table>
      : <p className="bases-hint">No setting changes.</p>)}
  </Dialog>;
}

function ArchiveBases({ type, keys, done }: { type: PresetType; keys: string[]; done(result: boolean | 'done'): void }) {
  const s = getState(), { busy, error, run } = useWrite(() => done('done'));
  const rows = keys.map((key) => ({ base: findProfile(s, type, key)!, children: childrenOf(s, type, key), to: baseOf(s, type, key) })).filter((r) => r.base && r.children.length);
  return <Dialog title="Remove bases" onClose={() => done(false)} error={error}
    actions={<button type="button" className="btn-danger" disabled={busy} onClick={() => run(() => archiveBases(type, keys))}>Move and remove</button>}>
    {rows.map((r) => <p key={profileKey(r.base)}>{r.children.map((c) => c.name).join(', ')} {r.children.length === 1 ? 'is' : 'are'} based on {r.base.name}. {r.children.length === 1 ? 'It moves' : 'They move'} to {r.to?.name ?? 'its system preset'}, keeping every value.</p>)}
  </Dialog>;
}

export const openMakeBase = (type: PresetType, key: string) => { const p = findProfile(getState(), type, key); return p ? host<void>((done) => <MakeBase type={type} profile={p} done={() => done()}/>) : Promise.resolve(); };
export const openNewBasedOn = (type: PresetType, from?: string) => host<void>((done) => <NewBasedOn type={type} from={from} done={() => done()}/>);
export const openChangeBase = (type: PresetType, key: string) => { const p = findProfile(getState(), type, key); return p ? host<void>((done) => <ChangeBase type={type} profile={p} done={() => done()}/>) : Promise.resolve(); };
export function confirmArchiveBases(type: PresetType, keys: string[]): Promise<boolean | 'done'> {
  const s = getState();
  if (!keys.some((key) => childrenOf(s, type, key).length)) return Promise.resolve(true);
  return host<boolean | 'done'>((done) => <ArchiveBases type={type} keys={keys} done={done}/>);
}
export async function confirmDetach(type: PresetType, key: string) {
  const p = findProfile(getState(), type, key), base = baseOf(getState(), type, key);
  if (!p || !base) return;
  if (await confirmDialog('Detach from base', `${p.name} keeps every value from ${base.name} as its own and no longer follows it.`, ['Detach', 'Cancel'])) detachFromBase(type, key).catch(failed);
}

export function baseMenuItems(type: PresetType, key: string): { label: string; action(): void; disabled?: boolean }[] {
  const p = findProfile(getState(), type, key);
  if (!p || p.kind === 'master') return [];
  return [
    { label: baseRoleOf(p) ? 'Base settings…' : 'Make this a base…', action: () => void openMakeBase(type, key) },
    { label: 'New profile based on this…', action: () => void openNewBasedOn(type, key) },
    { label: 'Change base…', action: () => void openChangeBase(type, key) },
    { label: 'Detach from base…', action: () => void confirmDetach(type, key), disabled: !p.parentId },
  ];
}
