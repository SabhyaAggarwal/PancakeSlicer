import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import { init } from './actions';
import { startProjects } from './flow/project';
import { loadTab } from './options';

void loadTab('process').catch(() => {});
void startProjects(init()).catch(() => {});


createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);