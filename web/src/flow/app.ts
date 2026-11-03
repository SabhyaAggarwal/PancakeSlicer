import { createMachine } from './fsm';
type AppState = 'booting' | 'empty' | 'importing' | 'ready' | 'failed';
type AppEvent = { type: 'BOOT' } | { type: 'READY'; hasObjects: boolean; importing: boolean }
  | { type: 'IMPORT' } | { type: 'SETTLED'; hasObjects: boolean } | { type: 'DOCUMENT'; hasObjects: boolean } | { type: 'FAIL'; error: string };
interface AppContext { error: string | null }
const ready = [
  { target: 'importing', guard: (_: AppContext, e: Extract<AppEvent, { type: 'READY' }>) => e.importing },
  { target: 'ready', guard: (_: AppContext, e: Extract<AppEvent, { type: 'READY' }>) => e.hasObjects },
  { target: 'empty' },
] as const;
const document = [
  { target: 'ready', guard: (_: AppContext, e: Extract<AppEvent, { type: 'DOCUMENT' | 'SETTLED' }>) => e.hasObjects }, { target: 'empty' },
] as const;
const common = {
  BOOT: { target: 'booting' as const, action: () => ({ error: null }) },
  READY: [...ready], IMPORT: 'importing' as const, DOCUMENT: [...document], SETTLED: [...document],
  FAIL: { target: 'failed' as const, action: (_: AppContext, e: { error: string }) => ({ error: e.error }) },
};
export const app = createMachine<AppState, AppEvent, AppContext>({
  id: 'app', initial: 'booting', context: { error: null },
  states: { booting: { on: { ...common, IMPORT: { action() {} }, DOCUMENT: { action() {} } } },
    empty: { on: common }, importing: { on: { ...common, DOCUMENT: { action() {} } } },
    ready: { on: common }, failed: { on: common } },
});
