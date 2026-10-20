import { lazy, Suspense, useEffect, useState } from 'react';
import { headLabel, loadHeadIndex } from '../heads';
import { useStore } from '../state/store';

const HeadDialog = lazy(() => import('./HeadDialog'));

export default function HeadPicker({ value, onCommit }: { value: string; onCommit: (id: string) => void }) {
  const heads = useStore((s) => s.heads);
  const [error, setError] = useState(false);
  useEffect(() => void loadHeadIndex().catch(() => setError(true)), []);
  const ids = heads?.map((h) => h.id) ?? [];
  if (!ids.includes(value)) ids.unshift(value);
  return (
    <select className="combo head-combo" value={value} onChange={(e) => onCommit(e.target.value)} title={error ? 'The head library failed to load' : headLabel(value)}>
      {ids.map((id) => <option key={id} value={id}>{headLabel(id)}</option>)}
    </select>
  );
}

export function HeadShapeButton({ head, layerHeight }: { head: string; layerHeight: number }) {
  const [open, setOpen] = useState(false);
  return <>
    <button type="button" className="side-btn" title="View the components and margins of the selected head" onClick={() => setOpen(true)}>View…</button>
    {open && <Suspense fallback={<div role="status">Loading head…</div>}><HeadDialog id={head} layerHeight={layerHeight} close={() => setOpen(false)} /></Suspense>}
  </>;
}
