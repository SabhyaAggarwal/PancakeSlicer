import { Popover } from 'radix-ui';
import { useMemo, useRef, useState } from 'react';
import { useAddSettingsRequest } from './add-settings-request';
import type { Config, ConfigValue } from '../api/types';
import objectKeys from '../object_options.json';
import { toolValue } from '../config-values';
import { display } from '../profiles/format';
import { effectiveConfig, getState, removeObjectOptions, removeVolumeOptions, selectionOf, setObjectOption, setState, setVolumeOption, useStore,
  isSelected, type LayerRange, type PlateObject, type State } from '../state/store';
import { undoable } from '../state/history';
import { overrideCount } from '../plater/overrides';
import { addRangeAfter, canAddAfter, deleteRange, editRange, rangeLabel, removeRangeOptions, setRangeOption } from '../plater/ranges';
import { Caret } from './Caret';
import { SettingsTab, type ProfileFields } from './SettingsTab';
import './object-overrides.css';

const REGION_KEYS = new Set(objectKeys.region);
const OBJECT_KEYS = new Set([...objectKeys.region, ...objectKeys.object]);
const plain = (name: string) => name.replace(/\.(stl|obj|3mf|drc|step|stp)$/i, '');
const editable = (o: PlateObject) => !o.project;
const instanceKeys = (o: PlateObject) => o.instances.map((i) => `${o.id}:${i.id}`);

export interface ObjectOverrides extends ProfileFields { keys: Set<string>; mixed: Record<string, string>;
  target: { kind: 'objects'; ids: number[] } | { kind: 'part'; object: number; volume: number } | { kind: 'range'; object: number; range: LayerRange } }

const same = (key: string, a: ConfigValue | undefined, b: ConfigValue | undefined) => a === b || (a !== undefined && b !== undefined && toolValue(key, a) === toolValue(key, b));

function overridesOf(s: State, project: Config): ObjectOverrides | null {
  const sel = s.selected, object = sel ? s.objects.find((o) => o.id === sel.object) : undefined;
  const single = (kind: 'part' | 'range', settings: Config, name: string, set: (k: string, v: ConfigValue) => void, remove: (keys: string[]) => void, target: ObjectOverrides['target']): ObjectOverrides => {
    const baseline = { ...project, ...object!.settings };
    return { name, keys: REGION_KEYS, mixed: {}, target, config: { ...baseline, ...settings }, baseline, changes: settings,
      set: (k, v) => undoable(kind === 'part' ? 'Edit part settings' : 'Edit range settings', () => same(k, v, baseline[k]) ? remove([k]) : set(k, v), `${name}:${k}`),
      reset: (k) => undoable('Reset to object', () => remove([k])) };
  };
  if (object && editable(object) && sel?.range !== undefined) {
    const range = object.ranges?.find((r) => r.id === sel.range);
    if (range) return single('range', range.settings, rangeLabel(range), (k, v) => setRangeOption(object.id, range.id, k, v), (keys) => removeRangeOptions(object.id, range.id, keys), { kind: 'range', object: object.id, range });
  }
  if (object && editable(object) && sel?.volume !== undefined) {
    const volume = object.volumes?.find((v) => v.id === sel.volume);
    if (volume && (volume.type === 'part' || volume.type === 'modifier'))
      return single('part', volume.settings ?? {}, volume.name, (k, v) => setVolumeOption(object.id, volume.id, k, v), (keys) => removeVolumeOptions(object.id, volume.id, keys), { kind: 'part', object: object.id, volume: volume.id });
  }
  const objects = s.objects.filter((o) => editable(o) && o.instances.some((i) => isSelected(sel, o.id, i.id)));
  if (!objects.length) return null;
  const ids = objects.map((o) => o.id);
  const config: Config = { ...project }, changes: Config = {}, mixed: Record<string, string> = {};
  const touched = new Set(objects.flatMap((o) => Object.keys(o.settings)));
  for (const k of touched) {
    const values = objects.map((o) => o.settings[k] ?? project[k]);
    config[k] = values[0];
    changes[k] = values[0];
    const distinct = [...new Map(values.filter((v) => v !== undefined).map((v) => [toolValue(k, v), v])).values()];
    if (distinct.length > 1) mixed[k] = distinct.slice(0, 3).map((v) => display(k, v)).join(', ') + (distinct.length > 3 ? ', …' : '');
  }
  return {
    name: objects.length === 1 ? plain(objects[0].name) : `${objects.length} objects`, keys: OBJECT_KEYS, mixed, target: { kind: 'objects', ids },
    config, baseline: project, changes,
    set: (k, v) => undoable('Edit object settings', () => objects.forEach((o) => same(k, v, project[k]) ? removeObjectOptions(o.id, [k]) : setObjectOption(o.id, k, v)), `objects:${ids}:${k}`),
    reset: (k) => undoable('Reset to project', () => objects.forEach((o) => removeObjectOptions(o.id, [k]))),
  };
}

export function useObjectOverrides(): ObjectOverrides | null {
  const objects = useStore((s) => s.objects), selected = useStore((s) => s.selected);
  const project = useStore((s) => effectiveConfig(s, 'process'));
  return useMemo(() => overridesOf(getState(), project), [objects, selected, project]);
}

export function ObjectPicker({ overrides }: { overrides: ObjectOverrides | null }) {
  const objects = useStore((s) => s.objects), selected = useStore((s) => s.selected);
  const [open, setOpen] = useState(false);
  const usable = objects.filter(editable);
  const chosen = new Set(usable.filter((o) => o.instances.some((i) => isSelected(selected, o.id, i.id)) && selected?.volume === undefined && selected?.range === undefined).map((o) => o.id));
  const pickObjects = (ids: number[]) => setState((s) => ({ selected: selectionOf(s, s.objects.filter((o) => ids.includes(o.id)).flatMap(instanceKeys)) }));
  const toggle = (o: PlateObject) => pickObjects(chosen.has(o.id) ? [...chosen].filter((id) => id !== o.id) : [...chosen, o.id]);
  const count = (n: number) => n > 0 && <span className="obj-pick-count" aria-label={`${n} ${n === 1 ? 'override' : 'overrides'}`}>{n}</span>;
  const row = (key: string, label: string, checked: boolean, n: number, onPick: () => void, depth = 0, check = true) =>
    <button key={key} type="button" role={check ? 'menuitemcheckbox' : 'menuitemradio'} aria-checked={checked} className="menu-item obj-pick-row" data-depth={depth || undefined}
      onClick={onPick}><span className="menu-check" aria-hidden>{checked ? '✓' : ''}</span><span className="orca-menu-label">{label}</span>{count(n)}</button>;
  return <Popover.Root open={open} onOpenChange={setOpen}>
    <Popover.Trigger asChild><button type="button" className="pk-trigger dropdown obj-pick" aria-label={`Objects: ${overrides?.name ?? 'none selected'}`}>
      <span className="pk-name">{overrides?.name ?? 'Select objects'}</span><Caret className="pk-caret"/></button></Popover.Trigger>
    <Popover.Portal><Popover.Content className="orca-menu obj-pick-pop" data-menu-family="orca" role="menu" aria-label="Objects" align="start" sideOffset={4} collisionPadding={8}
      onKeyDown={(e) => { if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'a') { e.preventDefault(); e.stopPropagation(); pickObjects(usable.map((o) => o.id)); } }}>
      {usable.map((o) => [
        row(`o${o.id}`, plain(o.name), chosen.has(o.id), overrideCount(o), () => toggle(o)),
        ...(o.volumes ?? []).filter((v) => v.type === 'part' || v.type === 'modifier').map((v) => row(`v${o.id}:${v.id}`, v.name, selected?.object === o.id && selected.volume === v.id, Object.keys(v.settings ?? {}).length,
          () => { setState({ selected: { object: o.id, instance: o.instances[0].id, volume: v.id } }); setOpen(false); }, 1, false)),
        ...(o.ranges ?? []).map((r) => row(`r${o.id}:${r.id}`, rangeLabel(r), selected?.object === o.id && selected.range === r.id, Object.keys(r.settings).length,
          () => { setState({ selected: { object: o.id, instance: o.instances[0].id, range: r.id } }); setOpen(false); }, 1, false)),
      ])}
    </Popover.Content></Popover.Portal>
  </Popover.Root>;
}

export function RangeEditor({ objectId, range }: { objectId: number; range: LayerRange }) {
  const object = useStore((s) => s.objects.find((o) => o.id === objectId));
  const field = (label: string, value: number, commit: (v: number) => boolean) => (
    <label className="form-row">
      <span>{label}</span>
      <input key={`${range.id}:${value}`} className="text" type="number" step={0.01} defaultValue={value} aria-label={label}
        onBlur={(e) => { const v = Number(e.target.value); if (v !== value && !(Number.isFinite(v) && commit(v))) e.target.value = String(value); }}
        onKeyDown={(e) => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); }} />
    </label>
  );
  return (
    <div className="range-editor">
      {field('Min Z (mm)', range.z[0], (v) => editRange(objectId, range.id, { z: [v, range.z[1]] }))}
      {field('Max Z (mm)', range.z[1], (v) => editRange(objectId, range.id, { z: [range.z[0], v] }))}
      {field('Layer height (mm)', range.layer_height, (v) => editRange(objectId, range.id, { layer_height: v }))}
      <span className="range-tools">
        <button type="button" className="btn" disabled={!object || !canAddAfter(object, range.id)} onClick={() => addRangeAfter(objectId, range.id)}>Add range</button>
        <button type="button" className="btn-ghost" onClick={() => deleteRange(objectId, range.id)}>Delete range</button>
      </span>
    </div>
  );
}


export default function ObjectScope() {
  const overrides = useObjectOverrides();
  const top = useRef<HTMLDivElement>(null);
  useAddSettingsRequest(!!overrides, () => top.current?.scrollIntoView({ block: 'start', behavior: 'smooth' }));
  return <>
    <div className="rail-row rail-row-wide" ref={top}><span className="preset-combo"><ObjectPicker overrides={overrides} /></span></div>
    {overrides?.target.kind === 'range' && <RangeEditor objectId={overrides.target.object} range={overrides.target.range} />}
    <div className="process-panel">{overrides ? <SettingsTab type="process" compact hidePreset overrides={overrides} />
      : <div className="settings-load" role="status">Select objects on the plate or in the list</div>}</div>
  </>;
}
