import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import { loadTab } from './options';
import './app.css';

void loadTab('process').catch(() => {});

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
