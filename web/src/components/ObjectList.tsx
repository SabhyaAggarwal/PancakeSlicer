import { useState } from 'react';
import { OPTIONS, useTab } from '../options';
import { effectiveConfig, getState, setState, updateInstance, updateVolume, togglePrintable, useStore } from '../state/store';
import { volumeIcon } from '../plater/volumes';
import { renameObject, setFilament, startRename } from '../edit';
import { canEditProject } from '../api/client';
import { undoable } from '../state/history';
import { filamentColor } from '../plater/filamentColor';
import { instanceBox } from '../plater/geometry';
import { Icon } from './Icon';
import { STYLE_NAMES, styleOf } from '../style-names';
import { plateResults } from '../recommend';

interface SliceRecord {
  objects?: { id: number; name: string; mode?: string; instances?: { id: number; shift_mm?: [number, number] }[] }[];
}

export function ObjectList() {
  useTab('process');
  const objects = useStore((s) => s.objects);
  const results = useStore(plateResults);
  const selected = useStore((s) => s.selected);
  const slots = useStore((s) => s.filamentSlots);
  const project = useStore((s) => s.project);
  const record = useStore((s) => (s.job.state === 'done' && !s.job.stale ? (s.job.status?.record as SliceRecord | undefined) : undefined));
  const renaming = useStore((s) => s.renaming);
  const openMenu = (e: React.MouseEvent, object: number, instance?: number, volume?: number) => {
    e.preventDefault();
    const o = getState().objects.find((ob) => ob.id === object)!;
    if (volume !== undefined) {
      setState({ selected: { object, instance: o.instances[0].id, allInstances: true, volume }, contextMenu: { x: e.clientX, y: e.clientY, object, volume } });
      return;
    }
    const sel = getState().selected;
    const inSelection = sel?.all || (sel?.object === object && (instance === undefined ? sel.allInstances : sel.instance === instance));
    setState({
      ...(inSelection ? {} : { selected: instance === undefined ? { object, instance: o.instances[0].id, allInstances: true } : { object, instance } }),
      contextMenu: { x: e.clientX, y: e.clientY, object, instance },
    });
  };
  const printableButton = (object: number, printable: boolean, instance?: number) => (
    <button className="ol-printable" aria-label="Printable" aria-pressed={printable}
      title="Click the icon to toggle printable properties of the object"
      onClick={(e) => { e.stopPropagation(); togglePrintable(object, instance); }}>
      <Icon name={printable ? 'check_on' : 'check_off_focused'} size={16} />
    </button>
  );
  const rowKeys = (pick: () => void, object: number, instance?: number) => ({
    tabIndex: 0,
    onKeyDown: (e: React.KeyboardEvent<HTMLDivElement>) => {
      if (e.target !== e.currentTarget) return;
      if (e.key === 'Enter' || e.key === ' ') pick();
      else if (e.key === 'ContextMenu' || (e.key === 'F10' && e.shiftKey)) {
        const r = e.currentTarget.getBoundingClientRect();
        openMenu({ preventDefault() {}, clientX: r.left + 24, clientY: r.bottom } as React.MouseEvent, object, instance);
      } else return;
      e.preventDefault();
    },
  });
  const [query, setQuery] = useState('');
  const [open, setOpen] = useState<Record<number, boolean>>({});
  const s = getState();
  const shown = objects.filter((o) => o.name.toLowerCase().includes(query.toLowerCase()));
  useStore((st) => st.dirty.process);
  useStore((st) => st.process);
  const global = effectiveConfig(s, 'process');
  const plateStyle = styleOf(global);

  return (
    <div className="object-list">
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
          const objSelected = (selected?.object === o.id && selected.volume === undefined) || !!selected?.all;
          const multi = o.instances.length > 1;
          const sinking = !o.project?.placeholder && o.instances.some((i) => { const b = instanceBox(o, i); return b.min.z < -0.001 && b.max.z >= -0.001; });
          const nSettings = Object.keys(o.settings).length;
          const style = o.project ? plateStyle : styleOf({ ...global, ...o.settings });
          return (
            <div key={o.id}>
              <div
                className={`ol-row ${objSelected ? 'selected' : ''}`}
                aria-selected={objSelected}
                {...rowKeys(() => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true } }), o.id)}
                onContextMenu={(e) => openMenu(e, o.id)}
                onClick={() => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true } })}
                onDoubleClick={() => startRename(o.id)}
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
                  {renaming === o.id ? (
                    <input className="ol-rename" aria-label="Object name" defaultValue={o.name} autoFocus
                      onFocus={(e) => e.target.select()}
                      onClick={(e) => e.stopPropagation()}
                      onBlur={(e) => renameObject(o.id, e.target.value)}
                      onKeyDown={(e) => {
                        if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
                        else if (e.key === 'Escape') setState({ renaming: null });
                      }} />
                  ) : (
                    <span className="ol-text" title={o.project?.placeholder ? `${o.name} — reading the mesh from the project` : o.name}>
                      {o.name}
                    </span>
                  )}
                  {style !== plateStyle && (
                    <span className="ol-badge" title={`Print style of this object; the plate prints ${STYLE_NAMES[plateStyle]}`}>
                      {STYLE_NAMES[style]}
                    </span>
                  )}
                  {results.length > 1 && results.find((r) => r.objectId === o.id) && (() => {
                    const r = results.find((x) => x.objectId === o.id)!;
                    return <span className="ol-meta" title={r.fact}>{r.pct}%</span>;
                  })()}
                  {o.project?.placeholder && (
                    <span className="ol-badge" title={o.project.missing ? 'The project holds no mesh this browser can read for this object' : undefined}>
                      {o.project.missing ? 'no mesh' : 'loading'}
                    </span>
                  )}
                </span>
                {printableButton(o.id, o.instances.some((i) => i.printable !== false))}
                <select
                  className="ol-fila-select"
                  disabled={!!o.project && !canEditProject()}
                  title={o.project && !canEditProject() ? 'Set in the 3MF project' : 'Filament'}
                  value={o.extruder}
                  style={{ borderLeftColor: filamentColor(s, o.extruder) }}
                  onClick={(e) => e.stopPropagation()}
                  onChange={(e) => setFilament(o.id, Number(e.target.value))}
                >
                  {slots.map((_, i) => (
                    <option key={i} value={i + 1}>
                      {i + 1}
                    </option>
                  ))}
                </select>
                <span className="ol-status">{sinking && <button className="ol-sinking" title="Click the icon to shift this object to the bed"
                  onClick={(e) => { e.stopPropagation(); undoable('Drop to bed', () => o.instances.forEach((i) => updateInstance(o.id, i.id, { z: 0 }))); }} disabled={!!o.project?.placeholder}>
                  <Icon name="objlist_sinking" size={16} />
                </button>}</span>
              </div>
              {nSettings > 0 && (
                <div
                  className={`ol-row ol-child ${objSelected && getState().processScope === 'objects' ? 'selected' : ''}`}
                  {...rowKeys(() => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true }, processScope: 'objects' }), o.id)}
                  onClick={() => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true }, processScope: 'objects' })}
                  title={Object.keys(o.settings).join(', ')}
                >
                  <Icon name="cog" size={14} />
                  <span className="ol-text">
                    Settings ({nSettings}): {[...new Set(Object.keys(o.settings).map((k) => OPTIONS[k]?.category || 'Others'))].join(', ')}
                  </span>
                </div>
              )}
              {o.volumes?.map((v) => {
                const pick = (scope?: 'objects') => setState({ selected: { object: o.id, instance: o.instances[0].id, allInstances: true, volume: v.id }, ...(scope ? { processScope: scope } : {}) });
                const nv = Object.keys(v.settings).length;
                const volSelected = selected?.object === o.id && selected.volume === v.id;
                return (
                  <div key={`v${v.id}`}>
                    <div className={`ol-row ol-child ${volSelected ? 'selected' : ''}`} aria-selected={volSelected}
                      {...rowKeys(() => pick(), o.id)}
                      onContextMenu={(e) => openMenu(e, o.id, undefined, v.id)}
                      onClick={() => pick()}
                      onDoubleClick={() => setState({ renaming: v.id })}>
                      <span className="ol-name">
                        <Icon name={volumeIcon(v.type)} size={14} />
                        {renaming === v.id ? (
                          <input className="ol-rename" aria-label="Part name" defaultValue={v.name} autoFocus
                            onFocus={(e) => e.target.select()}
                            onClick={(e) => e.stopPropagation()}
                            onBlur={(e) => { setState({ renaming: null }); if (e.target.value.trim() && e.target.value !== v.name) updateVolume(o.id, v.id, { name: e.target.value.trim() }, 'Rename Part'); }}
                            onKeyDown={(e) => {
                              if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
                              else if (e.key === 'Escape') setState({ renaming: null });
                            }} />
                        ) : <span className="ol-text" title={v.name}>{v.name}</span>}
                      </span>
                      <span className="ol-status" />
                      {v.type === 'part' ? (
                        <select className="ol-fila-select" title="Filament" value={v.extruder}
                          style={{ borderLeftColor: filamentColor(s, v.extruder || o.extruder) }}
                          onClick={(e) => e.stopPropagation()}
                          onChange={(e) => updateVolume(o.id, v.id, { extruder: Number(e.target.value) })}>
                          <option value={0}>-</option>
                          {slots.map((_, i) => <option key={i} value={i + 1}>{i + 1}</option>)}
                        </select>
                      ) : <span className="ol-fila" />}
                      <span className="ol-status" />
                    </div>
                    {nv > 0 && (
                      <div className={`ol-row ol-child2 ${volSelected && getState().processScope === 'objects' ? 'selected' : ''}`}
                        {...rowKeys(() => pick('objects'), o.id)} onClick={() => pick('objects')} title={Object.keys(v.settings).join(', ')}>
                        <Icon name="cog" size={14} />
                        <span className="ol-text">
                          Settings ({nv}): {[...new Set(Object.keys(v.settings).map((k) => OPTIONS[k]?.category || 'Others'))].join(', ')}
                        </span>
                      </div>
                    )}
                  </div>
                );
              })}
              {multi && open[o.id] && (
                <>
                  <div className="ol-row ol-child ol-group">Instances</div>
                  {o.instances.map((inst, k) => (
                    <div
                      key={inst.id}
                      className={`ol-row ol-child2 ${selected?.instance === inst.id ? 'selected' : ''}`}
                      aria-selected={selected?.instance === inst.id}
                      {...rowKeys(() => setState({ selected: { object: o.id, instance: inst.id } }), o.id, inst.id)}
                      onContextMenu={(e) => openMenu(e, o.id, inst.id)}
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
