import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import { init } from './actions';
import { startProjects } from './flow/project';
import { loadTab } from './options';
import { initialiseSavedPreferences } from './saved-preferences';
void initialiseSavedPreferences().catch(() => {});

void loadTab('process').catch(() => {});
void startProjects(init()).catch(() => {});


createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
window.addEventListener('pagehide', () => { void import('./saved-preferences').then(m=>m.flushSavedPreferences()).catch(()=>{}); });
