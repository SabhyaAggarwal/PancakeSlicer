import { useState } from 'react';
import { setState, useStore } from '../state/store';
import { ObjectList } from './ObjectList';
export default function ObjectsOutliner() {
  const [open, setOpen] = useState(true);
  const selected = useStore(s => !!s.selected && !s.selected.all);
  const gizmo = useStore(s => s.gizmo);
  const transforming = gizmo === 'move' || gizmo === 'rotate' || gizmo === 'scale';
  return <section className={`objects-outliner ${transforming ? 'transform-active' : ''}`} aria-label="Objects outliner">
    <div className="outliner-heading"><button type="button" className="outliner-toggle" aria-expanded={open} onClick={() => setOpen(!open)}>Objects <span aria-hidden>{open ? '▴' : '▾'}</span></button>{open && selected && <button type="button" className="btn-ghost outliner-edit" onClick={() => setState({processScope: 'objects'})}>Edit settings</button>}</div>
    {open && <div className="outliner-body"><ObjectList/></div>}
  </section>;
}
