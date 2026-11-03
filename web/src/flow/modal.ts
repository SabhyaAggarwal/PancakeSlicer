import { createMachine } from './fsm';
import { onLeaveDocument } from '../state/history';
interface ModalEntry { element: HTMLDialogElement; close(): void }
interface ModalContext { entries: ModalEntry[]; closing: HTMLDialogElement[] }
type ModalEvent = { type: 'PUSH'; entry: ModalEntry } | { type: 'REMOVE'; element: HTMLDialogElement }
  | { type: 'CLOSE'; element: HTMLDialogElement };
const remove = (c: ModalContext, e: { element: HTMLDialogElement }) => ({
  entries: c.entries.filter((d) => d.element !== e.element), closing: c.closing.filter((d) => d !== e.element),
});
const push = { target: 'open', action: (c: ModalContext, e: { entry: ModalEntry }) => ({
  entries: [...c.entries.filter((d) => d.element !== e.entry.element), e.entry],
  closing: c.closing.filter((d) => d !== e.entry.element),
}) } as const;
export const modals = createMachine<'closed' | 'open', ModalEvent, ModalContext>({
  id: 'modal', initial: 'closed', context: { entries: [], closing: [] },
  states: {
    closed: { on: { PUSH: push } },
    open: { on: {
      PUSH: push,
      REMOVE: [{ target: 'closed', guard: (c, e) => c.entries.every((d) => d.element === e.element), action: remove }, { action: remove }],
      CLOSE: { guard: (c, e) => c.entries.some((d) => d.element === e.element) && !c.closing.includes(e.element),
        action: (c, e) => {
          c.entries.find((d) => d.element === e.element)?.close();
          return { closing: [...c.closing, e.element] };
        } },
    } },
  },
});
export const topModalEntry = () => modals.get().context.entries.at(-1)?.element ?? null;
export const requestModalClose = (element: HTMLDialogElement) => modals.send({ type: 'CLOSE', element });
export function closeModals() {
  for (const entry of [...modals.get().context.entries].reverse()) requestModalClose(entry.element);
}
onLeaveDocument(closeModals);
window.addEventListener('popstate', closeModals);
window.addEventListener('hashchange', closeModals);
