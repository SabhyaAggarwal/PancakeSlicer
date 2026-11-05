import { useSyncExternalStore } from 'react';
import { APP_VERSION as current } from './build-info';
import { toast } from './state/store';

const REPO = 'ZimengXiong/PancakeSlicer';
const RELEASES = `https://github.com/${REPO}/releases`;
const EVERY_MS = 6 * 3600_000;
const CACHE = 'pancake.update.check', SEEN = 'pancake.update.seen', DEMO = 'pancake.update.demo';

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
    toast(`PancakeSlicer v${next.version} is available`, { label: 'View', run: () => openUpdate(next) }, 15_000, 'warning');
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

function target(release: Release): string {
  const page = release.html_url ?? RELEASES;
  if (!(window as { orcaDesktop?: unknown }).orcaDesktop) return page;
  const ua = navigator.userAgent;
  const kind = /Mac/.test(ua) ? /\.dmg$/i : /Windows/.test(ua) ? /\.(exe|msi)$/i : /\.(AppImage|deb)$/i;
  return release.assets?.find((a) => a.name && kind.test(a.name))?.browser_download_url ?? page;
}

async function latest(): Promise<Release | null> {
  try {
    const cached = JSON.parse(read(CACHE) ?? 'null') as { at: number; release: Release | null } | null;
    if (cached && Date.now() - cached.at < EVERY_MS) return cached.release;
  } catch {  }
  let release: Release | null = null;
  try {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers: { Accept: 'application/vnd.github+json' } });
    if (r.ok) release = await r.json() as Release;
  } catch {  }
  write(CACHE, JSON.stringify({ at: Date.now(), release }));
  return release;
}

let timer = 0, demoDefault = false, generation = 0;
async function check() {
  const mine = ++generation;
  const demo = read(DEMO);
  if (demo === '1' || (demo === null && demoDefault)) {
    const [major, minor] = semver(current) ?? [0, 0];
    return publish({ version: `${major}.${minor + 1}.0`, url: RELEASES });
  }
  const release = await latest();
  if (mine !== generation) return;
  publish(release?.tag_name && newer(release.tag_name, current) ? { version: release.tag_name.replace(/^v/, ''), url: target(release) } : null);
}

export function startUpdateChecks(demo: boolean) {
  demoDefault = demo;
  void check();
  if (!timer) timer = window.setInterval(() => void check(), EVERY_MS);
}

export const openUpdate = (u: Update) => void window.open(u.url, '_blank', 'noopener');
export const useUpdate = () => useSyncExternalStore((l) => { listeners.add(l); return () => { listeners.delete(l); }; }, () => available);
