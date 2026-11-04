import type { PresetType } from '../api/types';
import { switchPreset } from '../actions';
import { selectChanges } from '../state/changes';
import { getState, presetEdits, resolvePreset, setState, toast, type State } from '../state/store';
import { createProfileFromBase } from './catalog';
import { findProfile, profileKey } from './identity';
import { saveProfile } from './model';
import { acknowledgeProjectProfileChanges, captureProjectProfileChanges } from './project-draft';
import { archiveProfiles, commitProfileRecords } from './transactions';

const selectedKey = (s: State, type: PresetType, slot: number) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
const message = (e: unknown) => e instanceof Error ? e.message : String(e);

export function savesInPlace(s: State, type: PresetType, slot: number): boolean {
  const p = findProfile(s, type, selectedKey(s, type, slot));
  return !!p && p.kind !== 'master';
}

function changesOf(s: State, type: PresetType, slot: number) {
  return { edits: presetEdits(s, type, slot), project: (type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type]) ?? null };
}
function restoreChanges(type: PresetType, slot: number, key: string, c: ReturnType<typeof changesOf>) {
  setState((st) => ({
    ...(type === 'filament' ? { filamentEdits: { ...st.filamentEdits, [key]: c.edits } } : { dirty: { ...st.dirty, [type]: c.edits } }),
    ...(st.projectPresets && c.project ? { projectPresets: type === 'filament'
      ? { ...st.projectPresets, filaments: st.projectPresets.filaments.map((f, i) => i === slot ? c.project! : f) }
      : { ...st.projectPresets, [type]: c.project } } : {}),
  }));
}

export async function saveToProfile(type: PresetType, slot = 0): Promise<void> {
  const s = getState();
  if (!s.api) throw new Error('Not connected. Your changes are kept.');
  const stage = captureProjectProfileChanges(type, slot), source = findProfile(s, type, stage.key), resolved = resolvePreset(s, type, stage.key);
  if (!source || !resolved.base || resolved.error) throw new Error(resolved.error ?? 'This profile is unavailable. Your changes are kept.');
  const before = changesOf(s, type, slot), prior = structuredClone(source);
  const values = { ...resolved.config, ...stage.deltas };
  const record = { ...saveProfile(s, type, source.name, profileKey(source), values), id: profileKey(source), applicability: source.applicability };
  await commitProfileRecords([record], { apply: (st) => acknowledgeProjectProfileChanges(st, stage, values) });
  toast(`Saved to ${source.name}`, { label: 'Undo', run: () => void commitProfileRecords([{ ...prior, revision: findProfile(getState(), type, stage.key)?.revision }])
    .then(() => restoreChanges(type, slot, stage.key, before), (e) => toast(message(e))) });
}

export const newProfileName = (s: State, type: PresetType, slot: number) => {
  const r = resolvePreset(s, type, selectedKey(s, type, slot)), name = r.source?.name ?? r.base?.name ?? '';
  return name.startsWith('My ') ? name : `My ${name}`;
};

export function newProfileNameError(s: State, type: PresetType, name: string): string | null {
  const n = name.trim();
  if (!n) return 'Enter a name';
  return s.userPresets.some((p) => p.type === type && !p.archived && p.name === n) ? `“${n}” is already taken` : null;
}

export async function saveAsNewProfile(type: PresetType, slot: number, name: string): Promise<void> {
  const s = getState();
  if (!s.api) throw new Error('Not connected. Your changes are kept.');
  const error = newProfileNameError(s, type, name);
  if (error) throw new Error(error);
  const key = selectedKey(s, type, slot), resolved = resolvePreset(s, type, key);
  if (!resolved.base || resolved.error) throw new Error(resolved.error ?? 'This profile is unavailable. Your changes are kept.');
  const deltas = Object.fromEntries(selectChanges(s, type, slot).filter((c) => c.scope === 'project' && c.owner.plate === undefined).map((c) => [c.key, c.now]));
  const before = changesOf(s, type, slot);
  const created = await createProfileFromBase(type, resolved.base.name, name.trim(), { ...resolved.config, ...deltas });
  switchPreset(type, slot, created, 'discard');
  toast(`Saved as ${name.trim()}`, { label: 'Undo', run: () => {
    switchPreset(type, slot, key, 'discard');
    restoreChanges(type, slot, key, before);
    void archiveProfiles(type, [created]).catch((e) => toast(message(e)));
  } });
}
