import { checkBrowserSupport, showBrowserNotice, unsupportedBrowserMessage } from './browser-support';
import './theme';
import './styles/tokens.css';
import './styles/primitives.css';
import './app.css';

const root = document.getElementById('root')!;
checkBrowserSupport().then(() => {
  import('./main').catch(() => showBrowserNotice(root,
    'The application could not load. Check your connection and reload the page.', 'Unable to start OrcaSlicer'));
}, (error: unknown) => showBrowserNotice(root,
  unsupportedBrowserMessage(error instanceof Error ? error.message : String(error))));
