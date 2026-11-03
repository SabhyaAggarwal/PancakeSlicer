import { useState } from 'react';
import { setState, useStore } from '../state/store';
import { ObjectList } from './ObjectList';
import { Caret } from './Caret';
import { narrowQuery, showSettings } from '../workspace-sheet';
export default function ObjectsOutliner() {
  const [open, setOpen] = useState(() => !narrowQuery.matches);
  const selected = useStore(s => !!s.selected && !s.selected.all);
  const gizmo = useStore(s => s.gizmo);
  const transforming = gizmo === 'move' || gizmo === 'rotate' || gizmo === 'scale';
  return <section className={`objects-outliner ${transforming ? 'transform-active' : ''}`} aria-label="Objects outliner">
    <div className="outliner-heading"><button type="button" className="outliner-toggle" aria-expanded={open} onClick={() => setOpen(!open)}>Objects <Caret dir={open ? 'up' : 'down'}/></button>{open && selected && <button type="button" className="btn-ghost outliner-edit" onClick={() => {setState({processScope:'objects'});showSettings();}}>Settings</button>}</div>
    {open && <div className="outliner-body scroll-fade"><ObjectList/></div>}
  </section>;
}
