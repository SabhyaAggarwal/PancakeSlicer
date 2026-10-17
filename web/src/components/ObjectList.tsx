import { useState } from 'react';
import { getState, setState, updateObject, useStore } from '../state/store';
import { filamentColor } from '../plater/Plater3D';
import { Icon } from './Icon';

export function ObjectList() {
  const objects = useStore((s) => s.objects);
  const selected = useStore((s) => s.selected);
  const slots = useStore((s) => s.filamentSlots);
  const [query, setQuery] = useState('');
  const [open, setOpen] = useState<Record<number, boolean>>({});
  const s = getState();
  const shown = objects.filter((o) => o.name.toLowerCase().includes(query.toLowerCase()));

  return (
    <div className="object-list">
      <div className="search-bar">
        <Icon name="search" size={16} />
        <input placeholder="Search plate, object and part." value={query} onChange={(e) => setQuery(e.target.value)} />
      </div>
      <div className="ol-header">
        <span>Name</span>
        <span className="ol-fila">Fila.</span>
      </div>
      <div className="ol-body">
        {shown.map((o) => {
          const objSelected = selected?.object === o.id;
          const multi = o.instances.length > 1;
          return (
            <div key={o.id}>
              <div
                className={`ol-row ${objSelected ? 'selected' : ''}`}
                onClick={() => setState({ selected: { object: o.id, instance: o.instances[0].id } })}
              >
                <span className="ol-name">
                  {multi ? (
                    <button
                      className="ol-expand"
                      onClick={(e) => {
                        e.stopPropagation();
                        setOpen({ ...open, [o.id]: !open[o.id] });
                      }}
                    >
                      {open[o.id] ? '▾' : '▸'}
                    </button>
                  ) : (
                    <span className="ol-expand" />
                  )}
                  <Icon name="obj_printable" size={14} />
                  <span className="ol-text" title={o.name}>
                    {o.name}
                  </span>
                </span>
                <select
                  className="ol-fila-select"
                  value={o.extruder}
                  style={{ borderLeftColor: filamentColor(s, o.extruder) }}
                  onClick={(e) => e.stopPropagation()}
                  onChange={(e) => updateObject(o.id, { extruder: Number(e.target.value) })}
                >
                  {slots.map((_, i) => (
                    <option key={i} value={i + 1}>
                      {i + 1}
                    </option>
                  ))}
                </select>
              </div>
              {multi && open[o.id] && (
                <>
                  <div className="ol-row ol-child ol-group">Instances</div>
                  {o.instances.map((inst, k) => (
                    <div
                      key={inst.id}
                      className={`ol-row ol-child2 ${selected?.instance === inst.id ? 'selected' : ''}`}
                      onClick={() => setState({ selected: { object: o.id, instance: inst.id } })}
                    >
                      Instance {k + 1}
                    </div>
                  ))}
                </>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}
