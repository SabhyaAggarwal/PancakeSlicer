import { useEffect, useState } from 'react';

export function Num({ value, onCommit, step = 1 }: { value: number; onCommit: (v: number) => void; step?: number }) {
  const [text, setText] = useState(value.toFixed(2));
  useEffect(() => setText(value.toFixed(2)), [value]);
  return (
    <input
      className="text num manip-num"
      value={text}
      step={step}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => Number.isFinite(Number(text)) && onCommit(Number(text))}
      onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()}
    />
  );
}
