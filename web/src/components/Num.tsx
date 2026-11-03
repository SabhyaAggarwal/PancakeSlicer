import { useEffect, useState } from 'react';

export function Num({ value, onCommit, step = 1, label }: { value?: number; onCommit: (v: number) => void; step?: number; label?: string }) {
  const shown = value === undefined ? '' : value.toFixed(2);
  const [text, setText] = useState(shown);
  useEffect(() => setText(shown), [shown]);
  return (
    <input
      className="text num manip-num"
      aria-label={label}
      title={value === undefined ? "Select an editable object on the plate to change this value" : label}
      inputMode="decimal"
      disabled={value === undefined}
      placeholder={value === undefined ? '—' : undefined}
      value={text}
      step={step}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => Number.isFinite(Number(text)) && onCommit(Number(text))}
      onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
    />
  );
}
