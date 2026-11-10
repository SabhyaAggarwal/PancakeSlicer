import { useSyncExternalStore } from 'react';
import { APP_VERSION as current } from './build-info';
import { toast } from './state/store';

const REPO = 'ZimengXiong/PancakeSlicer';
const RELEASES = `https://github.com/${REPO}/releases`;
const EVERY_MS = 6 * 3600_000;
const SEEN = 'pancake.update.seen', DEMO = 'pancake.update.demo';

export interface Update { version: string; url: string }
interface Release { tag_name?: string; html_url?: string; assets?: { name?: string; browser_download_url?: string }[] }

let available: Update | null = null;
const listeners = new Set<() => void>();
const publish = (next: Update | null) => {
  if (next?.version === available?.version && next?.url === available?.url) return;
  available = next;
  listeners.forEach((l) => l());
  if (next && read(SEEN) !== next.version) {
    write(SEEN, next.version);
    toast(`New update available: PancakeSlicer v${next.version}!`, { label: 'View', run: () => openUpdate(next) }, 15_000, 'warning');
  }
};

const read = (key: string) => { try { return localStorage.getItem(key); } catch { return null; } };
const write = (key: string, value: string) => { try { localStorage.setItem(key, value); } catch {  } };

function semver(tag: string): number[] | null {
  const m = /^v?(\d+)\.(\d+)\.(\d+)(-[0-9A-Za-z.-]+)?$/.exec(tag.trim());
  return m ? [Number(m[1]), Number(m[2]), Number(m[3]), m[4] ? 0 : 1] : null;
}
const newer = (tag: string, than: string) => {
  const a = semver(tag), b = semver(than);
  if (!a || !b) return false;
  for (let i = 0; i < 4; i++) if (a[i] !== b[i]) return a[i] > b[i];
  return false;
};

/** The release page, where each system picks its own download. */
const target = (release: Release): string => release.html_url ?? RELEASES;

async function latest(): Promise<Release | null> {
  try {
    const r = await fetch('/api/update');
    return r.ok ? ((await r.json()) as { release: Release | null }).release : null;
  } catch { return null; }
}

let timer = 0, generation = 0;
async function check() {
  const mine = ++generation;
  const demo = read(DEMO);
  if (demo === '1') {
    const [major, minor] = semver(current) ?? [0, 0];
    return publish({ version: `${major}.${minor + 1}.0`, url: RELEASES });
  }
  const release = await latest();
  if (mine !== generation) return;
  publish(release?.tag_name && newer(release.tag_name, current) ? { version: release.tag_name.replace(/^v/, ''), url: target(release) } : null);
}

export function startUpdateChecks() {
  void check();
  if (!timer) timer = window.setInterval(() => void check(), EVERY_MS);
}

export const openUpdate = (u: Update) => void window.open(u.url, '_blank', 'noopener');
export const useUpdate = () => useSyncExternalStore((l) => { listeners.add(l); return () => { listeners.delete(l); }; }, () => available);
