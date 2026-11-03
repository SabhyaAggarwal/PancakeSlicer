import { createServiceDocument, type JsonValue } from './service-document';
import { ensureProjectAdoption } from './project-adoption';
export type SavedPreferences = Record<string, JsonValue>;
const preferences = createServiceDocument<SavedPreferences>('/api/preferences', 'PUT', () => ensureProjectAdoption());
export const servicePreferencesSnapshot = preferences.snapshot;
export const subscribeServicePreferences = preferences.subscribe;
export async function hydrateServicePreferences() { await ensureProjectAdoption(); return preferences.hydrate(); }
export const setServicePreferences = preferences.patch;
export async function flushServicePreferences(retry = false) { await ensureProjectAdoption(); return preferences.flush(retry); }
export const reloadServicePreferences = preferences.reload;
