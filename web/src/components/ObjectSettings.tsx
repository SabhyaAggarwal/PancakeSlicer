import { useEffect, useMemo, useRef, useState } from 'react';
import { Modal, ModalTitle } from './Modal';
import { menuKeys } from '../keys';
import type { Config, ConfigValue } from '../api/types';
import { isHidden, OPTIONS, useTab } from '../options';
import objectKeys from '../object_options.json';
import { effectiveConfig, effectiveMode, getState, removeObjectOptions, removeVolumeOptions, setObjectOption, setVolumeOption, useStore } from '../state/store';
import { volumeIcon } from '../plater/volumes';
import { undoable } from '../state/history';
import { environment, fieldDefinition, optionState } from '../visibility';
import { Field } from './Field';
import { Icon, IconButton } from './Icon';
import PrintStyle from './PrintStyle';

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
const asText = (v: ConfigValue | undefined) => (v === undefined ? '' : Array.isArray(v) ? v.join(',') : String(v));

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
  const { desc, error } = useTab('process');
  const object = useStore((s) => s.objects.find((o) => o.id === s.selected?.object));
  const volume = useStore((s) => object?.volumes?.find((v) => v.id === s.selected?.volume));
  const filaments = useStore((s) => s.filamentSlots.length);
  useStore((s) => s.dirty.process);
  useStore((s) => s.dirty.machine);
  useStore((s) => s.printer);
  const uiMode = useStore(effectiveMode);
  useStore((s) => s.process);
  const global = effectiveConfig(getState(), 'process');
  const [menu, setMenu] = useState(false);
  const [pick, setPick] = useState<[string, string[]] | null>(null);
  const cats = useMemo(() => hierarchy(filaments, volume ? objectKeys.region : OBJECT_KEYS), [filaments, desc, !!volume]);

  if (!desc) return <div role="status">{error ?? 'Loading settings…'}</div>;
  if (!object)
    return <div className="obj-settings-empty">Select an object in the list or on the plate to edit its settings.</div>;
  if (object.project)
    return (
      <div className="obj-settings-empty">
        {object.name} belongs to the opened 3MF project; its per-object settings travel inside the project file.
      </div>
    );

  if (volume && volume.type !== 'part' && volume.type !== 'modifier')
    return <div className="obj-settings-empty">{volume.name} has no settings.</div>;
  const target = volume ? {
    name: volume.name, icon: volumeIcon(volume.type), settings: volume.settings, region: true,
    set: (k: string, v: ConfigValue) => setVolumeOption(object.id, volume.id, k, v),
    remove: (keys: string[]) => removeVolumeOptions(object.id, volume.id, keys),
  } : {
    name: object.name, icon: 'obj_printable', settings: object.settings, region: false,
    set: (k: string, v: ConfigValue) => setObjectOption(object.id, k, v),
    remove: (keys: string[]) => removeObjectOptions(object.id, keys),
  };
  const add = (keys: string[], remove: string[] = []) => undoable('Add Settings', () => {
    for (const k of keys) if (!(k in target.settings) && (!target.region || objectKeys.region.includes(k))) target.set(k, global[k] ?? (OPTIONS[k]?.default as ConfigValue) ?? '');
    if (remove.length) target.remove(remove);
  });
  const state = getState();
  const machine = effectiveConfig(state, 'machine');
  const env = environment([machine, global, object.settings, ...(volume ? [volume.settings] : [])], {
    settings_scope: 'object', printer_vendor: state.machines.find((p) => p.name === state.printer)?.vendor,
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
        <Icon name={target.icon} size={14} />
        <span className="ol-text" title={target.name}>
          {target.name}
        </span>
        <span className="obj-settings-tools">
          <AddMenu
            open={menu}
            setOpen={setMenu}
            onBundle={(keys) => add(keys)}
            cats={cats}
            onCategory={(c) => setPick(c)}
          />
          <IconButton
            icon="delete"
            title={volume ? 'Delete all settings of this part' : 'Delete all settings of this object'}
            disabled={!Object.keys(target.settings).length}
            onClick={() => target.remove(Object.keys(target.settings))}
          />
        </span>
      </div>
      {!volume && <PrintStyle />}
      {!Object.keys(target.settings).length && (
        <div className="obj-settings-empty">{volume ? 'This part uses its object\'s settings.' : 'This object uses the global process settings.'} Add settings with “+”.</div>
      )}
      <div className="page-body">
        {[...byCat.entries()].map(([cat, keys]) => (
          <section key={cat} className="optgroup">
            <header className="optgroup-title">
              <span>{cat}</span>
            </header>
            {keys.map((k) => (
              <Field
                key={k}
                optKey={k}
                type="process"
                disabled={states[k]?.enabled === false}
                definition={fieldDefinition(k, env)}
                value={target.settings[k]}
                systemValue={global[k]}
                dirty={asText(target.settings[k]) !== asText(global[k])}
                onSet={(key, v) => target.set(key, v)}
                onReset={(key) => target.set(key, global[key] ?? target.settings[key])}
                resetTitle="Reset to the global process value"
                extra={
                  <button type="button" className="icon-btn opt-remove" title="Remove this setting from the object" onClick={() => target.remove([k])}>
                    ×
                  </button>
                }
              />
            ))}
          </section>
        ))}
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

function AddMenu(props: {
  open: boolean;
  setOpen(v: boolean): void;
  onBundle(keys: string[]): void;
  cats: [string, string[]][];
  onCategory(c: [string, string[]]): void;
}) {
  const ref = useRef<HTMLSpanElement>(null);
  useEffect(() => {
    if (!props.open) return;
    const close = (e: MouseEvent) => !ref.current?.contains(e.target as Node) && props.setOpen(false);
    window.addEventListener('mousedown', close);
    ref.current?.querySelector<HTMLElement>('.menu-pop .menu-item')?.focus();
    return () => window.removeEventListener('mousedown', close);
  }, [props.open]);
  const item = (label: string, fn: () => void, icon?: string) => (
    <button
      key={label}
      className="menu-item"
      onClick={() => {
        props.setOpen(false);
        fn();
      }}
    >
      <span className="menu-label">
        {icon ? <Icon name={icon} size={14} /> : <span style={{ width: 14 }} />}
        {label}
      </span>
    </button>
  );
  return (
    <span className="menu" ref={ref}
      onKeyDown={(e) => props.open && menuKeys(e, ref.current!.querySelector('.menu-pop'), () => props.setOpen(false), ref.current!.querySelector('button'))}>
      <IconButton icon="add" title="Add settings" onClick={() => props.setOpen(!props.open)} />
      {props.open && (
        <div className="menu-pop menu-pop-right" role="menu" aria-label="Add settings">
          {FREQ_BUNDLE.map(([cat, keys]) => item(cat, () => props.onBundle(keys.filter((k) => OPTIONS[k] && !isHidden(k))), cat === 'Support' ? 'support' : undefined))}
          <div className="menu-sep" />
          <div className="menu-caption">Add settings by category</div>
          {props.cats.map((c) => item(`${c[0]}…`, () => props.onCategory(c), c[0] === 'Support' ? 'support' : undefined))}
        </div>
      )}
    </span>
  );
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
    <Modal className="small" label={`Select settings — ${props.title}`} onClose={props.onClose} backdropClose returnFocus='.obj-settings-tools button[title="Add settings"]'>
        <ModalTitle title={`Select settings — ${props.title}`} onClose={props.onClose} />
        <div className="select-list">
          {props.keys.map((k) => (
            <label key={k} className="select-row" title={OPTIONS[k]?.tooltip}>
              <input type="checkbox" checked={checked.has(k)} onChange={() => toggle(k)} />
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
