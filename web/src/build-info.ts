

import { useSyncExternalStore } from 'react';
import { version } from '../package.json';

export const APP_VERSION = version;
export const GITHUB_URL = 'https://github.com/ZimengXiong/PancakeSlicer';
export const DOCS_URL = '/help/index.html';

export type BuildAbout = { version: string; revision: string; image: string; license: string; source_url: string; source_configured: boolean; source_sha256: string };
export interface BuildInfo { state: 'loading' | 'ready' | 'failed'; about?: BuildAbout; engine?: string; engineSha?: string }

let info: BuildInfo = { state: 'loading' }, asked = false;
const listeners = new Set<() => void>();
const json = (path: string) => fetch(path).then((r) => (r.ok ? r.json() : null)).catch(() => null);
function load() {
  if (asked) return;
  asked = true;
  void Promise.all([json('/api/about'), json('/api/healthz')]).then(([about, health]) => {
    const manifest = (health as { artifact_manifest?: { backend?: string; source_sha?: string } } | null)?.artifact_manifest;
    info = { state: about ? 'ready' : 'failed', about: about ?? undefined, engine: manifest?.backend, engineSha: manifest?.source_sha };
    listeners.forEach((l) => l());
  });
}

export function reloadBuildInfo() { asked = false; info = { state: 'loading' }; listeners.forEach((l) => l()); load(); }
export function useBuildInfo(): BuildInfo {
  load();
  return useSyncExternalStore((l) => { listeners.add(l); return () => { listeners.delete(l); }; }, () => info);
}

export const shortSha = (sha?: string) => sha?.slice(0, 7);

export function buildLine(i: BuildInfo): string {
  return [`PancakeSlicer v${APP_VERSION}`, i.about?.revision && `build ${shortSha(i.about.revision)}`,
    i.engine && `engine ${i.engine}${i.engineSha ? ` ${shortSha(i.engineSha)}` : ''}`].filter(Boolean).join(' · ');
}
