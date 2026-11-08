import { createMachine, type StateNode } from './fsm';

export type ProfileSaveState = 'idle' | 'saving' | 'failed' | 'naming' | 'renaming';
interface ProfileSaveContext { request: number; error: string | null; name: string; nameError: string | null }
type ProfileSaveEvent = { type: 'SAVE' } | { type: 'REJECT'; error: string } | { type: 'SAVED'; request: number }
  | { type: 'FAIL'; request: number; error: string } | { type: 'RENAME'; name: string } | { type: 'TYPE'; value: string }
  | { type: 'COMMIT'; value: string; current: string; taken: boolean } | { type: 'RENAMED'; request: number }
  | { type: 'RENAME_FAIL'; request: number; error: string } | { type: 'CANCEL' } | { type: 'DISMISS' };
type Node = StateNode<ProfileSaveState, ProfileSaveEvent, ProfileSaveContext>;

const mine = (c: ProfileSaveContext, e: { request: number }) => e.request === c.request;
const late = {} as const;
const save = { target: 'saving', action: (c: ProfileSaveContext) => ({ request: c.request + 1, error: null }) } as const;
const rename = { target: 'naming', action: (_: ProfileSaveContext, e: Extract<ProfileSaveEvent, { type: 'RENAME' }>) => ({ name: e.name, nameError: null }) } as const;
const reject = { target: 'failed', action: (_: ProfileSaveContext, e: Extract<ProfileSaveEvent, { type: 'REJECT' }>) => ({ error: e.error }) } as const;
const results = { SAVED: late, FAIL: late, RENAMED: late, RENAME_FAIL: late, DISMISS: late } as const;
const ready: Node = { on: { ...results, SAVE: save, REJECT: reject, RENAME: rename, DISMISS: { target: 'idle', action: () => ({ error: null }) } } };

export const createProfileSave = () => createMachine<ProfileSaveState, ProfileSaveEvent, ProfileSaveContext>({
  id: 'profile-save', initial: 'idle', strict: true, context: { request: 0, error: null, name: '', nameError: null },
  states: {
    idle: ready,
    failed: ready,
    saving: { on: { ...results,
      SAVED: [{ target: 'idle', guard: mine }, late],
      FAIL: [{ target: 'failed', guard: mine, action: (_, e) => ({ error: e.error }) }, late],
    } },
    naming: { on: { ...results,
      TYPE: { action: (_, e) => ({ name: e.value, nameError: null }) },
      COMMIT: [
        { target: 'idle', guard: (_, e) => e.value.trim() === e.current },
        { guard: (_, e) => !e.value.trim(), action: () => ({ nameError: 'Enter a name' }) },
        { guard: (_, e) => e.taken, action: (_, e) => ({ nameError: `“${e.value.trim()}” is already taken` }) },
        { target: 'renaming', action: (c) => ({ request: c.request + 1 }) },
      ],
      CANCEL: { target: 'idle', action: () => ({ nameError: null }) },
    } },
    renaming: { on: { ...results, COMMIT: late,
      RENAMED: [{ target: 'idle', guard: mine, action: () => ({ nameError: null }) }, late],
      RENAME_FAIL: [{ target: 'naming', guard: mine, action: (_, e) => ({ nameError: e.error }) }, late],
    } },
  },
});
