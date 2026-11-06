import { checkBrowserSupport, showBrowserNotice, unsupportedBrowserMessage } from './browser-support';
import './theme';
document.documentElement.classList.add('orca-parity');
import './styles/tokens.css';
import './styles/primitives.css';
import './app.css';
import './mobile.css';
import './components/controls.css';
import { registerServiceWorker } from './sw';

const root = document.getElementById('root')!;
registerServiceWorker();
window.addEventListener('load', () => void import('./build-update').then((m) => m.watchForNewBuild()), { once: true });


if (typeof Worker !== 'undefined') void import('./api/worker').then((m) => m.catalogWorker()).catch(() => {});
checkBrowserSupport().then(() => {
  import('./main').catch(() => showBrowserNotice(root,
    'The application could not load. Check your connection and reload the page.', 'Unable to start PancakeSlicer'));
}, (error: unknown) => showBrowserNotice(root,
  unsupportedBrowserMessage(error instanceof Error ? error.message : String(error))));
