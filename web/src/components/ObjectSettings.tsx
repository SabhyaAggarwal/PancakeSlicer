import { Check } from './Check';
import { useMemo, useRef, useState } from 'react';
import { Modal, ModalTitle } from './Modal';
import { ActionMenu } from './Menu';
import type { Config, ConfigValue } from '../api/types';
import { isHidden, OPTIONS, useTab } from '../options';
import objectKeys from '../object_options.json';
import { effectiveConfig, effectiveMode, getState, removeObjectOptions, removeVolumeOptions, setObjectOption, setVolumeOption, useStore } from '../state/store';
import { volumeIcon } from '../plater/volumes';
import { undoable } from '../state/history';
import { environment, fieldDefinition, optionState } from '../visibility';
import { nativeSupportCapable } from '../settings-env';
import { Field } from './Field';
import { GroupHead, isGroupOpen, setGroupOpen, useSettingsGroups } from './SettingsGroup';
import { addRangeAfter, canAddAfter, deleteRange, editRange, rangeLabel, removeRangeOptions, setRangeOption } from '../plater/ranges';
import type { LayerRange } from '../state/store';
import { settingValue } from '../state/changes';
import { useChanges } from '../state/useChanges';

const OBJECT_KEYS = [...objectKeys.region, ...objectKeys.object];

const FREQ_BUNDLE: [string, string[]][] = [
  ['Quality', ['layer_height']],
  ['Shell', ['wall_loops', 'top_shell_layers', 'bottom_shell_layers']],
  ['Infill', ['sparse_infill_density', 'sparse_infill_pattern']],
  ['Support', ['enable_support', 'support_type', 'support_threshold_angle', 'support_threshold_overlap', 'support_base_pattern',
    'support_on_build_plate_only', 'support_critical_regions_only', 'support_remove_small_overhang', 'support_base_pattern_spacing',
    'support_expansion']],
  ['Flush options', ['flush_into_infill', 'flush_into_objects', 'flush_into_support']],
];

const labelOf = (key: string) => OPTIONS[key]?.full_label || OPTIONS[key]?.label || key;

function hierarchy(filaments: number, keys: string[]): [string, string[]][] {
  const cats = new Map<string, string[]>();
  for (const key of keys) {
    const def = OPTIONS[key];
    if (!def || isHidden(key)) continue;
    const cat = def.category ?? '';
    if (!cat || (filaments === 1 && (cat === 'Extruders' || cat === 'Wipe options'))) continue;
    if (!cats.has(cat)) cats.set(cat, []);
    cats.get(cat)!.push(key);
  }
  return [...cats.entries()].sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
}

export function ObjectSettings() {
  useSettingsGroups();
  const { desc, error } = useTab('process');
  const object = useStore((s) => s.objects.find((o) => o.id === s.selected?.object));
  const volume = useStore((s) => object?.volumes?.find((v) => v.id === s.selected?.volume));
  const range = useStore((s) => object?.ranges?.find((r) => r.id === s.selected?.range));
  const filaments = useStore((s) => s.filamentSlots.length);
  useStore((s) => s.dirty.process);
  useStore((s) => s.dirty.machine);
  useStore((s) => s.printer);
  const uiMode = useStore(effectiveMode);
  useStore((s) => s.process);
  const changes = useChanges('process');
  const global = effectiveConfig(getState(), 'process');
  const [menu, setMenu] = useState(false);
  const [pick, setPick] = useState<[string, string[]] | null>(null);
  const cats = useMemo(() => hierarchy(filaments, volume || range ? objectKeys.region : OBJECT_KEYS), [filaments, desc, !!(volume || range)]);

  if (!desc) return <div role="status">{error ?? 'Loading settings…'}</div>;
  if (!object)
    return <div className="obj-settings-empty">No selection</div>;
  if (object.project)
    return (
      <div className="obj-settings-empty">
        {object.name} belongs to the opened 3MF project; its per-object settings travel inside the project file.
      </div>
    );

  if (volume && volume.type !== 'part' && volume.type !== 'modifier')
    return <div className="obj-settings-empty">{volume.name} has no settings.</div>;
  const target = range ? {
    name: rangeLabel(range), icon: 'height_range_layer', settings: range.settings, region: true,
    set: (k: string, v: ConfigValue) => setRangeOption(object.id, range.id, k, v),
    remove: (keys: string[]) => removeRangeOptions(object.id, range.id, keys),
  } : volume ? {
    name: volume.name, icon: volumeIcon(volume.type, volume.seamMode), settings: volume.settings, region: true,
    set: (k: string, v: ConfigValue) => setVolumeOption(object.id, volume.id, k, v),
    remove: (keys: string[]) => removeVolumeOptions(object.id, volume.id, keys),
  } : {
    name: object.name, icon: 'obj_printable', settings: object.settings, region: false,
    set: (k: string, v: ConfigValue) => setObjectOption(object.id, k, v),
    remove: (keys: string[]) => removeObjectOptions(object.id, keys),
  };
  const inherited = (key: string) => settingValue(getState(), 'process', key, 0, volume || range ? { object: object.id } : {});
  const changed = new Set(changes.filter((c) => c.owner.object === object.id && (range ? c.scope === 'range' && c.owner.range === range.id : volume ? c.scope === 'part' && c.owner.volume === volume.id : c.scope === 'object')).map((c) => c.key));
  const add = (keys: string[], remove: string[] = []) => {
    undoable('Add Settings', () => {
      for (const k of keys) if (!(k in target.settings) && (!target.region || objectKeys.region.includes(k))) target.set(k, inherited(k) ?? '');
      if (remove.length) target.remove(remove);
    });
    for (const k of keys) setGroupOpen(`object/${OPTIONS[k]?.category || 'Others'}`, true);
  };
  const state = getState();
  const machine = effectiveConfig(state, 'machine');
  const env = environment([machine, global, object.settings, ...(volume ? [volume.settings] : []), ...(range ? [range.settings] : [])], {
    settings_scope: 'object', printer_vendor: state.machines.find((p) => p.name === state.printer)?.vendor, native_support_capable: nativeSupportCapable(state),
  }, uiMode);
  const states = optionState(desc, env);
  const byCat = new Map<string, string[]>();
  for (const k of Object.keys(target.settings)) {
    if (isHidden(k)) continue;
    const cat = OPTIONS[k]?.category || 'Others';
    if (!byCat.has(cat)) byCat.set(cat, []);
    byCat.get(cat)!.push(k);
  }

  return (
    <div className="obj-settings">
      <div className="obj-settings-head">
        <span className="override-context">{range ? 'Layer overrides' : volume ? 'Part overrides' : 'Object overrides'}</span>
        <span className="obj-settings-tools">
          <AddMenu
            open={menu}
            setOpen={setMenu}
            onBundle={(keys) => add(keys)}
            cats={cats}
            onCategory={(c) => setPick(c)}
          />
          {Object.keys(target.settings).length > 0 && <button type="button" className="btn-ghost"
            onClick={() => target.remove(Object.keys(target.settings))}>Clear</button>}
        </span>
      </div>
      
      {range && <RangeEditor objectId={object.id} range={range} canAdd={canAddAfter(object, range.id)} />}
      {!Object.keys(target.settings).length && (
        <div className="obj-settings-empty">{range || volume ? 'Uses the object values' : 'Uses the project values'}</div>
      )}
      <div className="page-body">
        {[...byCat.entries()].map(([cat, keys]) => { const id = `object/${cat}`, open = isGroupOpen(id); return (
          <section key={cat} className="optgroup" data-collapsed={open ? undefined : ''}>
            <GroupHead id={id} title={cat} modified={keys.some((k) => changed.has(k))} open={open} />
            {open && keys.map((k) => (
              <Field
                key={k}
                optKey={k}
                type="process"
                disabled={states[k]?.enabled === false}
                definition={fieldDefinition(k, env)}
                value={target.settings[k]}
                systemValue={inherited(k)}
                dirty={changed.has(k)}
                onSet={(key, v) => target.set(key, v)}
                scope={{ target: { object: object.id, volume: volume?.id, range: range?.id } }}
              />
            ))}
          </section>
        ); })}
      </div>
      {pick && (
        <SelectSettings
          title={pick[0]}
          keys={pick[1]}
          current={target.settings}
          onClose={() => setPick(null)}
          onOk={(checked) => {
            add(checked, pick[1].filter((k) => k in target.settings && !checked.includes(k)));
            setPick(null);
          }}
        />
      )}
    </div>
  );
}

function RangeEditor({ objectId, range, canAdd }: { objectId: number; range: LayerRange; canAdd: boolean }) {
  const field = (label: string, value: number, commit: (v: number) => boolean) => (
    <label className="prefs-row">
      <span>{label}</span>
      <input key={`${range.id}:${value}`} className="text" type="number" step={0.01} defaultValue={value} aria-label={label}
        onBlur={(e) => { const v = Number(e.target.value); if (v !== value && !(Number.isFinite(v) && commit(v))) e.target.value = String(value); }}
        onKeyDown={(e) => { if (e.key === 'Enter') (e.target as HTMLInputElement).blur(); }} />
      <span>mm</span>
    </label>
  );
  return (
    <div className="range-editor">
      {field('Min Z', range.z[0], (v) => editRange(objectId, range.id, { z: [v, range.z[1]] }))}
      {field('Max Z', range.z[1], (v) => editRange(objectId, range.id, { z: [range.z[0], v] }))}
      {field('Layer height', range.layer_height, (v) => editRange(objectId, range.id, { layer_height: v }))}
      <span className="range-tools">
        <button type="button" className="btn" disabled={!canAdd} onClick={() => addRangeAfter(objectId, range.id)}>Add range</button>
        <button type="button" className="btn" onClick={() => deleteRange(objectId, range.id)}>Delete range</button>
      </span>
    </div>
  );
}

function AddMenu(props: {
  open: boolean;
  setOpen(v: boolean): void;
  onBundle(keys: string[]): void;
  cats: [string, string[]][];
  onCategory(c: [string, string[]]): void;
}) {
  const ref = useRef<HTMLSpanElement>(null);
  return <span className="menu" ref={ref}>
    <button type="button" className="btn" aria-haspopup="menu" aria-expanded={props.open} onClick={() => props.setOpen(!props.open)}>Add settings</button>
    {props.open && <ActionMenu label="Add settings" anchor={ref.current?.querySelector('button')} onClose={() => props.setOpen(false)} items={[
      { label: 'Quick groups', header: true },
      ...FREQ_BUNDLE.map(([cat, keys]) => ({ label: cat, action: () => props.onBundle(keys.filter((k) => OPTIONS[k] && !isHidden(k))) })),
      { label: 'Choose settings', header: true },
      ...props.cats.map((c) => ({ label: c[0], action: () => props.onCategory(c) })),
    ]} />}
  </span>;
}

function SelectSettings(props: { title: string; keys: string[]; current: Config; onOk(keys: string[]): void; onClose(): void }) {
  const [checked, setChecked] = useState(() => new Set(props.keys.filter((k) => k in props.current)));
  const toggle = (k: string) => {
    const next = new Set(checked);
    if (next.has(k)) next.delete(k);
    else next.add(k);
    setChecked(next);
  };
  return (
    <Modal size="m" label={`Select settings — ${props.title}`} onClose={props.onClose} backdropClose returnFocus='.obj-settings-tools button[title="Add settings"]'>
        <ModalTitle title={`Select settings — ${props.title}`} onClose={props.onClose} />
        <div className="select-list">
          {props.keys.map((k) => (
            <label key={k} className="select-row">
              <Check checked={checked.has(k)} onCheckedChange={() => toggle(k)} />
              <span>{labelOf(k)}</span>
            </label>
          ))}
        </div>
        <div className="modal-buttons">
          <button className="btn" onClick={() => setChecked(new Set(checked.size === props.keys.length ? [] : props.keys))}>
            {checked.size === props.keys.length ? 'Deselect all' : 'Select all'}
          </button>
          <span style={{ flex: 1 }} />
          <button className="btn primary" onClick={() => props.onOk([...checked])}>
            OK
          </button>
          <button className="btn" onClick={props.onClose}>
            Cancel
          </button>
        </div>
    </Modal>
  );
}
