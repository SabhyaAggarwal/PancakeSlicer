import './caret.css';

export function Caret({ dir = 'down', className = '' }: { dir?: 'down' | 'right' | 'up'; className?: string }) {
  return <span aria-hidden className={`caret caret-${dir} ${className}`}>
    <svg viewBox="0 0 10 10" width="10" height="10"><path d="M2 3.5 5 6.5 8 3.5" /></svg>
  </span>;
}
