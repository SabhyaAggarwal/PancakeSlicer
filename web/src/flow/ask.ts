import { createMachine } from './fsm';
import { onLeaveDocument } from '../state/history';
export type Ask = { title: string; message?: string; value?: string; range?: [number, number]; choices?: string[]; buttons?: [string, string | null]; resolve(v: string | null): void };
type AskEvent = { type: 'ASK'; request: Ask } | { type: 'ANSWER'; request: Ask; value: string | null } | { type: 'CANCEL' };
const ask = { target: 'open', action: (c: { request: Ask | null }, e: { request: Ask }) => {
  c.request?.resolve(null); return { request: e.request };
} } as const;
export const asking = createMachine<'closed' | 'open', AskEvent, { request: Ask | null }>({
  id: 'ask', initial: 'closed', context: { request: null },
  states: {
    closed: { on: { ASK: ask } },
    open: { on: {
      ASK: ask,
      ANSWER: { target: 'closed', guard: (c, e) => c.request === e.request,
        action: (c, e) => { c.request!.resolve(e.value); return { request: null }; } },
      CANCEL: { target: 'closed', action: (c) => { c.request?.resolve(null); return { request: null }; } },
    } },
  },
});
export const askQuestion = (a: Omit<Ask, 'resolve'>) => new Promise<string | null>((resolve) => asking.send({ type: 'ASK', request: { ...a, resolve } }));
const cancel = () => asking.send({ type: 'CANCEL' });
onLeaveDocument(cancel);
window.addEventListener('popstate', cancel);
window.addEventListener('hashchange', cancel);
