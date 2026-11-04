import { projectDraftMutationSnapshot } from './persist/drafts';
import { getState, toast } from './state/store';

const CHECK_MS = 5 * 60_000, MIN_GAP_MS = 15_000;

export function watchForNewBuild(): void {
  const loaded = document.querySelector<HTMLMetaElement>('meta[name="orca-build"]')?.content;
  if (!loaded) return;
  let last = 0, newer = false, held = false;
  const safe = () => !held && projectDraftMutationSnapshot().pending === 0 && !['submitting', 'running'].includes(getState().job.state);
  const apply = () => {
    if (!newer) return;
    if (safe()) location.reload();
    else toast('Update ready', { label: 'Reload', run: () => location.reload() }, 20_000, 'warning');
  };
  const check = async () => {
    if (document.visibilityState !== 'visible' || Date.now() - last < MIN_GAP_MS) return;
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
  window.addEventListener('focus', () => void check());
  document.addEventListener('visibilitychange', () => void check());
  setInterval(() => void check(), CHECK_MS);
}
