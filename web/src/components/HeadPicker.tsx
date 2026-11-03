import { Dropdown } from './Dropdown';
import { lazy, Suspense, useEffect, useState } from 'react';
import { headLabel, loadHeadIndex } from '../heads';
import { setOption, useStore } from '../state/store';

const HeadDialog = lazy(() => import('./HeadDialog'));

export default function HeadPicker({ value, onCommit }: { value: string; onCommit: (id: string) => void }) {
  const heads = useStore((s) => s.heads);
  const [error, setError] = useState(false);
  useEffect(() => void loadHeadIndex().catch(() => setError(true)), []);
  const ids = heads?.map((h) => h.id) ?? [];
  if (!ids.includes(value)) ids.unshift(value);
  return (
    <Dropdown aria-label="Print head" className="combo head-combo" value={value} onValueChange={onCommit} title={error ? 'The head library failed to load' : headLabel(value)} options={[...ids.map((id) => ({ value: id, label: headLabel(id) }))]} />
  );
}

export function HeadShapeButton({ head, layerHeight, onSaved = id => setOption('machine', 'nonplanar_head', id) }: { head: string; layerHeight: number; onSaved?(id: string): void }) {
  const [open, setOpen] = useState(false);
  return <>
    <button type="button" className="side-btn" title="Edit the components and margins of the selected head and save them as a user head." onClick={() => setOpen(true)}>Edit…</button>
    {open && <Suspense fallback={<div role="status">Loading head…</div>}>
      <HeadDialog id={head} layerHeight={layerHeight} close={() => setOpen(false)} onSaved={onSaved} />
    </Suspense>}
  </>;
}
