import type { SendState } from './versions';

const PLANE = 'M14 2.5 2.5 7.25l4.4 1.85 1.85 4.4zM14 2.5 6.9 9.1';
const PRINTER = 'M4.5 6V2.5h7V6M2 6h12v5.5H2zM4.5 9.5h7v4h-7z';
const CHECK = 'M9 12l2 2 4-4.5';
const CROSS = 'M10.25 9.75l4.5 4.5M14.75 9.75l-4.5 4.5';

export function SendMark({ state, size = 14 }: { state: SendState; size?: number }) {
  const mark = state === 'printed' ? CHECK : state === 'print_failed' || state === 'failed' ? CROSS : null;
  const body = state === 'sending' || state === 'failed' ? PLANE : PRINTER;
  return <svg className="vh-send-mark" data-state={state} width={size} height={size} viewBox="0 0 16 16" aria-hidden="true" fill="none" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round">
    <path d={body} stroke="currentColor" transform={mark && body === PRINTER ? 'translate(-.5 -2) scale(.85)' : undefined} />
    {mark && <path d={mark} className={state === 'printed' ? undefined : 'vh-send-bad'} stroke="currentColor" />}
  </svg>;
}
