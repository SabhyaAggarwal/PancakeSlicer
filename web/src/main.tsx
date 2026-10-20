import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import { init } from './actions';
import { loadTab } from './options';

void loadTab('process').catch(() => {});
void init().then(() => import('./persist/session')).then((m) => m.start()).catch(() => {});

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
