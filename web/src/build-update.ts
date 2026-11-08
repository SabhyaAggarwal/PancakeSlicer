import { projectDraftMutationSnapshot } from './persist/drafts';
import { getState, toast } from './state/store';

const CHECK_MS = 5 * 60_000, MIN_GAP_MS = 15_000;

export function watchForNewBuild(): void {
  const loaded = document.querySelector<HTMLMetaElement>('meta[name="orca-build"]')?.content;
  if (!loaded) return;
  let last = 0, newer = false, held = false;
  const safe = () => { const drafts = projectDraftMutationSnapshot(); return !held && drafts.pending === 0 && drafts.error === null && !['submitting', 'running'].includes(getState().job.state); };
  const apply = () => {
    if (!newer) return;
    if (safe()) location.reload();
    else toast('Update ready', { label: 'Reload', run: () => location.reload() }, 20_000, 'warning');
  };
  const check = async (now = false) => {
    if (!now && (document.visibilityState !== 'visible' || Date.now() - last < MIN_GAP_MS)) return;
    last = Date.now();
    try {
      const current = (await fetch('/', { method: 'HEAD', cache: 'no-store' })).headers.get('X-Build');
      newer = !!current && current !== loaded;
    } catch { return; }
    apply();
  };
  window.addEventListener('pointerdown', () => { held = true; }, true);
  window.addEventListener('pointerup', () => { held = false; }, true);
  window.addEventListener('pointercancel', () => { held = false; }, true);
  window.addEventListener('vite:preloadError', () => void check(true).then(() => {
    let tries = 0;
    const retry = () => { if (newer && !safe() && ++tries <= 30) setTimeout(() => (safe() ? location.reload() : retry()), 1000); };
    retry();
  }));
  window.addEventListener('focus', () => void check());
  document.addEventListener('visibilitychange', () => void check());
  setInterval(() => void check(), CHECK_MS);
}
