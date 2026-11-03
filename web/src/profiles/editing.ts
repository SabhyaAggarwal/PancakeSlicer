import type { PresetType } from '../api/types';
import { accessSnapshot } from '../api/access';
import { createMachine, type Machine } from '../flow/fsm';
import { getState, resolvePreset, saveUserPreset, toast } from '../state/store';

export type EditState = 'clean' | 'modified' | 'naming' | 'saving' | 'saved' | 'failed';
export type EditEvent =
  | { type: 'EDITS'; n: number }
  | { type: 'SAVE' }
  | { type: 'SAVE_AS' }
  | { type: 'NAMED'; name: string; detach: boolean }
  | { type: 'CANCEL' }
  | { type: 'DONE' }
  | { type: 'FAIL'; error: string };
export interface EditContext { kind: PresetType; slot: number; n: number; name: string; detach: boolean; error: string }

const currentName = (kind: PresetType, slot: number) => {
  const s = getState();
  return (kind === 'machine' ? s.printer : kind === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
};
const isMine = (c: EditContext) => !!resolvePreset(getState(), c.kind, currentName(c.kind, c.slot)).user;

function define(kind: PresetType, slot: number) {
  return createMachine<EditState, EditEvent, EditContext>({
    id: `preset:${kind}:${slot}`,
    initial: 'clean',
    context: { kind, slot, n: 0, name: '', detach: false, error: '' },
    states: {
      clean: { on: {
        EDITS: [{ target: 'modified', guard: (_, e) => e.n > 0, action: (_, e) => ({ n: e.n }) }, { action: (_, e) => ({ n: e.n }) }],
        SAVE_AS: 'naming',
      } },
      modified: { on: {
        EDITS: [{ target: 'clean', guard: (_, e) => e.n === 0, action: () => ({ n: 0 }) }, { action: (_, e) => ({ n: e.n }) }],
        SAVE: [{ target: 'saving', guard: isMine, action: (c) => ({ name: currentName(c.kind, c.slot), detach: false }) }, { target: 'naming' }],
        SAVE_AS: 'naming',
      } },
      naming: {
        note: 'the Save dialog is open',
        on: {
          NAMED: { target: 'saving', action: (_, e) => ({ name: e.name, detach: e.detach }) },
          CANCEL: [{ target: 'modified', guard: (c) => c.n > 0 }, { target: 'clean' }],
          EDITS: { action: (_, e) => ({ n: e.n }) },
        },
      },
      saving: {
        entry: (c, _, m) => {
          saveUserPreset(c.kind, c.name, c.slot, c.detach).then(() => m.send({ type: 'DONE' }), (e: unknown) => {
            const error = e instanceof Error ? e.message : String(e);
            if (accessSnapshot()?.message !== error) toast(`Save failed: ${error}`);
            m.send({ type: 'FAIL', error });
          });
          return { error: '' };
        },
        on: {
          DONE: { target: 'saved', action: (c) => void toast(`Saved "${c.name}"`) },
          FAIL: { target: 'failed', action: (_, e) => ({ error: e.error }) },
          EDITS: { action: (_, e) => ({ n: e.n }) },
        },
      },
      saved: {
        after: { ms: 1500, target: 'clean' },
        on: { EDITS: [{ target: 'modified', guard: (_, e) => e.n > 0, action: (_, e) => ({ n: e.n }) }, { action: (_, e) => ({ n: e.n }) }] },
      },
      failed: { on: {
        NAMED: { target: 'saving', action: (_, e) => ({ name: e.name, detach: e.detach }) },
        SAVE: 'saving',
        CANCEL: { target: 'modified' },
        EDITS: { action: (_, e) => ({ n: e.n }) },
      } },
    },
  });
}

const machines = new Map<string, Machine<EditState, EditEvent, EditContext>>();

export function editing(kind: PresetType, slot = 0) {
  const id = `${kind}:${slot}`;
  let m = machines.get(id);
  if (!m) machines.set(id, (m = define(kind, slot)));
  return m;
}
