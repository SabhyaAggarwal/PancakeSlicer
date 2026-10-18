import { useEffect, useRef, useState } from 'react';
import { OPTIONS, useTab } from '../options';
import { getState, setState, updateObject, updateInstance, togglePrintable, useStore } from '../state/store';
import { filamentColor } from '../plater/filamentColor';
import { instanceBox } from '../plater/geometry';
import { Icon } from './Icon';

interface SliceRecord {
  objects?: { id: number; name: string; mode?: string; instances?: { id: number; shift_mm?: [number, number] }[] }[];
}

export function ObjectList() {
  useTab('process');
  const objects = useStore((s) => s.objects);
  const selected = useStore((s) => s.selected);
  const slots = useStore((s) => s.filamentSlots);
  const project = useStore((s) => s.project);
  const record = useStore((s) => (s.job.state === 'done' && !s.job.stale ? (s.job.status?.record as SliceRecord | undefined) : undefined));
  const [menu, setMenu] = useState<{ x: number; y: number; object: number; instance?: number } | null>(null);
  const menuRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!menu) return;
    const close = (e: MouseEvent) => { if (!menuRef.current?.contains(e.target as Node)) setMenu(null); };
    const escape = (e: KeyboardEvent) => { if (e.key === 'Escape') setMenu(null); };
    window.addEventListener('mousedown', close);
    window.addEventListener('keydown', escape);
    return () => { window.removeEventListener('mousedown', close); window.removeEventListener('keydown', escape); };
  }, [menu]);
  const printableButton = (object: number, printable: boolean, instance?: number) => (
    <button className="ol-printable" aria-label="Printable" aria-pressed={printable}
      title="Click the icon to toggle printable properties of the object"
      onClick={(e) => { e.stopPropagation(); togglePrintable(object, instance); }}>
      <Icon name={printable ? 'check_on' : 'check_off_focused'} size={16} />
    </button>
  );
  const [query, setQuery] = useState('');
  const [open, setOpen] = useState<Record<number, boolean>>({});
  const s = getState();
  const menuPrintable = objects.find((o) => o.id === menu?.object)?.instances.find((i) => menu?.instance === undefined || i.id === menu.instance)?.printable !== false;
  const shown = objects.filter((o) => o.name.toLowerCase().includes(query.toLowerCase()));

  return (
    <div className="object-list">
      {menu && <div ref={menuRef} role="menu" className="menu-pop ol-menu" style={{ left: Math.min(menu.x, window.innerWidth - 180), top: Math.min(menu.y, window.innerHeight - 50) }}>
        <button className="menu-item" role="menuitemcheckbox"
          aria-checked={menuPrintable}
          onClick={() => { togglePrintable(menu.object, menu.instance); setMenu(null); }}>
          <span>{menuPrintable ? '✓ ' : '　'}Printable</span><span>V</span>
        </button>
      </div>}
      <div className="search-bar">
        <Icon name="search" size={16} />
        <input placeholder="Search plate, object and part." value={query} onChange={(e) => setQuery(e.target.value)} />
      </div>
      <div className="ol-header">
        <span className="ol-name">Name</span>
        <span className="ol-status" />
        <span className="ol-fila">Fila.</span>
        <span className="ol-status" />
      </div>
      <div className="ol-body">
        {shown.map((o) => {
          const objSelected = selected?.object === o.id;
          const multi = o.instances.length > 1;
          const sinking = !o.project?.placeholder && o.instances.some((i) => { const b = instanceBox(o, i); return b.min.z < -0.001 && b.max.z >= -0.001; });
          const nSettings = Object.keys(o.settings).length;
          return (
            <div key={o.id}>
              <div
                className={`ol-row ${objSelected ? 'selected' : ''}`}
                onContextMenu={(e) => { e.preventDefault(); setMenu({ x: e.clientX, y: e.clientY, object: o.id }); }}
                onClick={() => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true } })}
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
                {printableButton(o.id, o.instances.some((i) => i.printable !== false))}
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
                <span className="ol-status">{sinking && <button className="ol-sinking" title="Click the icon to shift this object to the bed"
                  onClick={(e) => { e.stopPropagation(); o.instances.forEach((i) => updateInstance(o.id, i.id, { z: 0 })); }} disabled={!!o.project}>
                  <Icon name="objlist_sinking" size={16} />
                </button>}</span>
              </div>
              {nSettings > 0 && (
                <div
                  className={`ol-row ol-child ${objSelected && getState().processScope === 'objects' ? 'selected' : ''}`}
                  onClick={() => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true }, processScope: 'objects' })}
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
                      onContextMenu={(e) => { e.preventDefault(); setMenu({ x: e.clientX, y: e.clientY, object: o.id, instance: inst.id }); }}
                      onClick={() => setState({ selected: { object: o.id, instance: inst.id } })}
                    >
                      <span className="ol-name">Instance {k + 1}</span>
                      {printableButton(o.id, inst.printable !== false, inst.id)}
                      <span className="ol-fila" /><span className="ol-status" />
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
