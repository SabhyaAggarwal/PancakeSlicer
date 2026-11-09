import { useEffect, useState } from 'react';

export function Num({ value, onCommit, step = 1, label, axis }: { value?: number; onCommit: (v: number) => void; step?: number; label?: string; axis?: 'X' | 'Y' | 'Z' }) {
  const shown = value === undefined ? '' : value.toFixed(2);
  const [text, setText] = useState(shown);
  useEffect(() => setText(shown), [shown]);
  const input = (
    <input
      className="text num manip-num"
      aria-label={label}
      inputMode="decimal"
      disabled={value === undefined}
      placeholder={value === undefined ? '—' : undefined}
      value={text}
      step={step}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => (text.trim() !== '' && Number.isFinite(Number(text)) ? onCommit(Number(text)) : setText(shown))}
      onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
    />
  );
  return axis ? <span className="manip-axis"><span className={`axis ${axis.toLowerCase()}`} aria-hidden>{axis}</span>{input}</span> : input;
}
