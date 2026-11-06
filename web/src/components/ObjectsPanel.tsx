



import { useEffect, useState } from 'react';
import { useStore } from '../state/store';
import { narrowQuery } from '../workspace-sheet';
import { ObjectList } from './ObjectList';
import { Caret } from './Caret';

const KEY = 'orca.objects-panel';
const saved = () => { try { return sessionStorage.getItem(KEY); } catch { return null; } };

export default function ObjectsPanel() {
  const count = useStore((s) => s.objects.length);
  const gizmo = useStore((s) => !!s.gizmo && s.gizmo !== 'none');
  const [open, setOpen] = useState(() => (saved() ?? (narrowQuery.matches ? '0' : '1')) === '1');

  const [peek, setPeek] = useState(false);
  useEffect(() => setPeek(false), [gizmo]);
  const shown = open && (!gizmo || peek);
  const toggle = () => {
    if (gizmo && open && !peek) return setPeek(true);
    try { sessionStorage.setItem(KEY, shown ? '0' : '1'); } catch {                                    }
    setOpen(!shown);
    setPeek(!shown);
  };
  return (
    <section className="objects-panel" data-open={shown || undefined} aria-label="Objects">
      <button type="button" className="op-head" aria-expanded={shown} onClick={toggle}>
        <span className="op-title">Objects</span><span className="op-count">{count}</span><Caret dir={shown ? 'up' : 'down'} />
      </button>
      {shown && <div className="op-body scroll-fade"><ObjectList /></div>}
    </section>
  );
}
