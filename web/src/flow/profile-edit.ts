import type { PresetType } from '../api/types';
import { profileConflicts, type ProfileConflict } from '../profiles/model';
import { getState, presetEdits, subscribeStore } from '../state/store';
import { createMachine, type Machine } from './fsm';
type ProfileState = 'clean' | 'overlay-dirty' | 'saving' | 'reset' | 'conflict';
type ProfileEvent = { type: 'SYNC'; dirty: boolean; conflicts: ProfileConflict[] } | { type: 'SAVE' }
  | { type: 'RESET' } | { type: 'RETRY' } | { type: 'FAIL'; error: string };
interface ProfileContext { conflicts: ProfileConflict[]; dirty: boolean; error: string | null }
const sync = [
  { target: 'conflict', guard: (_: ProfileContext, e: Extract<ProfileEvent, { type: 'SYNC' }>) => e.conflicts.length > 0,
    action: (_: ProfileContext, e: Extract<ProfileEvent, { type: 'SYNC' }>) => ({ conflicts: e.conflicts, dirty: e.dirty }) },
  { target: 'overlay-dirty', guard: (_: ProfileContext, e: Extract<ProfileEvent, { type: 'SYNC' }>) => e.dirty,
    action: () => ({ conflicts: [], dirty: true }) },
  { target: 'clean', action: () => ({ conflicts: [], dirty: false, error: null }) },
] as const;
const common = { SYNC: [...sync], RESET: { target: 'reset' as const } };
export const createProfileEdit = () => createMachine<ProfileState, ProfileEvent, ProfileContext>({
  id: 'profile-edit', initial: 'clean', context: { conflicts: [], dirty: false, error: null },
  states: {
    clean: { on: common },
    'overlay-dirty': { on: { ...common, SAVE: 'saving', RETRY: 'saving' } },
    saving: { on: { ...common, FAIL: { target: 'overlay-dirty', action: (_, e) => ({ error: e.error }) } } },
    reset: { on: common },
    conflict: { note: 'resolve with profiles/model.ts before saving', on: common },
  },
});
const machines = new Map<string, Machine<ProfileState, ProfileEvent, ProfileContext>>();
export function profileEdit(type: PresetType, slot = 0) {
  const key = `${type}:${slot}`;
  let m = machines.get(key);
  if (m) return m;
  const machine = createProfileEdit(); machines.set(key, machine);
  let wasDirty = false;
  const syncStore = () => {
    const s = getState(), name = type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot];
    const conflicts = name ? profileConflicts(s, type, name) : [];
    const dirty = Object.keys(presetEdits(s, type, slot)).length > 0;
    if (wasDirty && !dirty) machine.send({ type: 'RESET' });
    wasDirty = dirty;
    machine.send({ type: 'SYNC', dirty, conflicts });
  };

  subscribeStore(syncStore); syncStore();
  return machine;
}
export function startProfileEdits() {
  profileEdit('machine'); profileEdit('process');
  let count = 0;
  const sync = () => { while (count < getState().filamentSlots.length) profileEdit('filament', count++); };
  subscribeStore(sync); sync();
}
