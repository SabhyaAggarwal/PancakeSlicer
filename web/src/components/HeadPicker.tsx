import { Dropdown } from './Dropdown';
import { preloadable } from '../preloadable';
import { Suspense, useEffect, useState } from 'react';
import { headLabel, loadHeadIndex } from '../heads';
import { setOption, useStore } from '../state/store';

const HeadDialog = preloadable(() => import('./HeadDialog'));

export default function HeadPicker({ value, onCommit }: { value: string; onCommit: (id: string) => void }) {
  const heads = useStore((s) => s.heads);
  const [error, setError] = useState(false);
  useEffect(() => void loadHeadIndex().catch(() => setError(true)), []);
  const ids = heads?.map((h) => h.id) ?? [];
  if (!ids.includes(value)) ids.unshift(value);
  return (
    <Dropdown aria-label="Print head" className="combo head-combo" value={value} onValueChange={onCommit} title={error ? 'Oops! Couldn\'t load the print head library' : headLabel(value)} options={[...ids.map((id) => ({ value: id, label: headLabel(id) }))]} />
  );
}

export function HeadShapeButton({ head, layerHeight, onSaved = id => setOption('machine', 'nonplanar_head', id) }: { head: string; layerHeight: number; onSaved?(id: string): void }) {
  const [open, setOpen] = useState(false);
  return <>
    <button type="button" className="side-btn" title="Customize the dimensions and safety margins of this print head and save it as your own profile." onClick={() => setOpen(true)}>Edit…</button>
    {open && <Suspense fallback={<div role="status">Loading head…</div>}>
      <HeadDialog id={head} layerHeight={layerHeight} close={() => setOpen(false)} onSaved={onSaved} />
    </Suspense>}
  </>;
}
