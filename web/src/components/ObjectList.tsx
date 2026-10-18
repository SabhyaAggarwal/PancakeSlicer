import { useState } from 'react';
import { OPTIONS } from '../options';
import { getState, setState, updateObject, useStore } from '../state/store';
import { filamentColor } from '../plater/Plater3D';
import { Icon } from './Icon';

interface SliceRecord {
  objects?: { id: number; name: string; mode?: string; instances?: { id: number; shift_mm?: [number, number] }[] }[];
}

export function ObjectList() {
  const objects = useStore((s) => s.objects);
  const selected = useStore((s) => s.selected);
  const slots = useStore((s) => s.filamentSlots);
  const project = useStore((s) => s.project);
  const record = useStore((s) => (s.job.state === 'done' && !s.job.stale ? (s.job.status?.record as SliceRecord | undefined) : undefined));
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
          const nSettings = Object.keys(o.settings).length;
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
                  <span className="ol-text" title={o.project?.placeholder ? `${o.name} — reading the mesh from the project` : o.name}>
                    {o.name}
                  </span>
                  {o.project?.placeholder && (
                    <span className="ol-badge" title={o.project.missing ? 'The project holds no mesh this browser can read for this object' : undefined}>
                      {o.project.missing ? 'no mesh' : 'loading'}
                    </span>
                  )}
                </span>
                <select
                  className="ol-fila-select"
                  disabled={!!o.project}
                  title={o.project ? 'Set in the 3MF project' : 'Filament'}
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
              {nSettings > 0 && (
                <div
                  className={`ol-row ol-child ${objSelected && getState().processScope === 'objects' ? 'selected' : ''}`}
                  onClick={() => setState({ selected: { object: o.id, instance: o.instances[0].id }, processScope: 'objects' })}
                  title={Object.keys(o.settings).join(', ')}
                >
                  <Icon name="cog" size={14} />
                  <span className="ol-text">
                    Settings ({nSettings}): {[...new Set(Object.keys(o.settings).map((k) => OPTIONS[k]?.category || 'Others'))].join(', ')}
                  </span>
                </div>
              )}
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
        {project && record?.objects && (
          <div className="ol-record">
            <div className="ol-row ol-group">Sliced from {project.name}</div>
            {record.objects.map((r) => (
              <div key={r.id} className="ol-row ol-child" title={r.instances?.map((i) => i.shift_mm?.map((v) => v.toFixed(1)).join(', ')).join(' · ')}>
                <Icon name="obj_printable" size={14} />
                <span className="ol-text">{r.name}</span>
                <span className="ol-meta">
                  {r.instances?.length ?? 1}× {r.mode ?? ''}
                </span>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
