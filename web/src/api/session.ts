import { randomId } from '../random-id';
export const sessionId: string = randomId().replace(/-/g, '');
const KEY = 'orca.sessions', KEEP = 8, lockName = (id: string) => `orca-session:${id}`;
let prior: string[] = [];
try {
  const stored: unknown = JSON.parse(sessionStorage.getItem(KEY) ?? '[]');
  prior = Array.isArray(stored) ? stored.filter((v): v is string => typeof v === 'string').slice(0, KEEP) : [];
  sessionStorage.setItem(KEY, JSON.stringify([sessionId, ...prior].slice(0, KEEP)));
} catch {  }
if (typeof navigator !== 'undefined') void navigator.locks?.request(lockName(sessionId), () => new Promise<never>(() => {})).catch(() => {});
export async function predecessorSessions(): Promise<string[]> {
  if (!prior.length || typeof navigator === 'undefined' || !navigator.locks) return [];
  const state = await navigator.locks.query().catch(() => null);
  if (!state) return [];
  const live = new Set([...(state.held ?? []), ...(state.pending ?? [])].map((lock) => lock.name));
  return prior.some((id) => live.has(lockName(id))) ? [] : prior;
}
