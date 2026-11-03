import { useSyncExternalStore } from 'react';
import type { JsonValue } from './persist/service-document';
type Controller = typeof import('./persist/preferences');
type Status = { state: 'loading' | 'ready' | 'pending' | 'failed'; error: string | null };
let values: Record<string, JsonValue> = {}, queued: Record<string, JsonValue> = {};
let status: Status = { state: 'loading', error: null }, controller: Controller | null = null;
let loading: Promise<void> | null = null;
const listeners = new Set<() => void>();
const keyListeners = new Map<string, Set<() => void>>();
const same = (a: unknown, b: unknown) => a === b || JSON.stringify(a) === JSON.stringify(b);
function publish(next: Record<string, JsonValue>, nextStatus: Status) {
  const previous = values; values = next; status = nextStatus;
  for (const key of new Set([...Object.keys(previous), ...Object.keys(next)]))
    if (!same(previous[key], next[key])) keyListeners.get(key)?.forEach(listener => listener());
  listeners.forEach(listener => listener());
}
export const preference = (key: string) => values[key];
export function preferenceText(key: string): string | undefined {
  const value = preference(key); return value == null ? undefined : typeof value === 'string' ? value : String(value);
}
export function preferenceJson<T>(key: string, fallback: T): T {
  const value = preference(key);
  if (value === undefined || value === null) return fallback;
  if (typeof value !== 'string') return value as T;
  try { return JSON.parse(value) as T; } catch { return fallback; }
}
export function subscribePreference(key: string, listener: () => void) {
  const group = keyListeners.get(key) ?? new Set(); group.add(listener); keyListeners.set(key, group);
  return () => { group.delete(listener); if (!group.size) keyListeners.delete(key); };
}
export function observePreference(key: string, listener: () => void) { listener(); return subscribePreference(key, listener); }
export const usePreference = (key: string) => useSyncExternalStore(listener => subscribePreference(key, listener), () => preference(key));
export const savedPreferencesSnapshot = () => status;
export const subscribeSavedPreferences = (listener: () => void) => { listeners.add(listener); return () => { listeners.delete(listener); }; };
function sync() {
  if (!controller) return;
  const snapshot = controller.servicePreferencesSnapshot();
  publish({ ...snapshot.document, ...queued }, { state: Object.keys(queued).length && snapshot.state === 'ready' ? 'pending' : snapshot.state, error: snapshot.error });
}
export function savePreferences(patch: Record<string, JsonValue>) {
  const changed = Object.fromEntries(Object.entries(patch).filter(([key, value]) => !same(values[key], value)));
  if (!Object.keys(changed).length) return;
  if (controller) controller.setServicePreferences(changed);
  else { queued = { ...queued, ...structuredClone(changed) }; publish({ ...values, ...queued }, { ...status, state: status.state === 'failed' ? 'failed' : 'pending' }); }
}
export function initialiseSavedPreferences(retry = false): Promise<void> {
  if (loading && !retry) return loading;
  loading = (async () => {
    const adoption = await import('./persist/project-adoption');
    await adoption.ensureProjectAdoption(retry);
    if (!controller) {
      const loaded = await import('./persist/preferences');
      const edits = queued; queued = {}; controller = loaded;
      if (Object.keys(edits).length) controller.setServicePreferences(edits);
      controller.subscribeServicePreferences(sync); sync();
    }
    await controller.hydrateServicePreferences();
    sync(); if (retry) await controller.flushServicePreferences(true);
  })().catch((error: unknown) => {
    loading = null; publish(values, { state: 'failed', error: error instanceof Error ? error.message : String(error) }); throw error;
  });
  return loading;
}
export async function flushSavedPreferences() { await initialiseSavedPreferences(); await controller!.flushServicePreferences(); }
