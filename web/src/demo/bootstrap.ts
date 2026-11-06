import { configureDemo, installDemoTransport } from './transport';
installDemoTransport();
configureDemo({
  notice: message => { void import('../state/store').then(m => m.toast(message)); },
  resolve: async input => (await import('./resolve')).resolveDemo(input),
});

void import('../bootstrap');

document.addEventListener('click', (event) => {
  const target = (event.target as Element | null)?.closest('button,[role="menuitem"]');
  const label = target?.textContent?.trim() ?? '';
  if (!target || !/^(Export\b|Send(?:\.\.\.|…)?$|Save Project\b)/i.test(label)) return;
  event.preventDefault(); event.stopImmediatePropagation();
  void import('../state/store').then(({ toast }) => toast('Not available in the demo', null, 3500, 'warning'));
}, true);
import './demo.css';
