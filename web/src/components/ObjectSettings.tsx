import { useEffect, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue } from '../api/types';
import { isHidden, OPTIONS } from '../options';
import objectKeys from '../object_options.json';
import { effectiveConfig, getState, removeObjectOptions, setObjectOption, useStore } from '../state/store';
import { Field } from './Field';
import { Icon, IconButton } from './Icon';

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

function hierarchy(filaments: number): [string, string[]][] {
  const cats = new Map<string, string[]>();
  for (const key of OBJECT_KEYS) {
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
  const object = useStore((s) => s.objects.find((o) => o.id === s.selected?.object));
  const filaments = useStore((s) => s.filamentSlots.length);
  useStore((s) => s.dirty.process);
  useStore((s) => s.process);
  const global = effectiveConfig(getState(), 'process');
  const [menu, setMenu] = useState(false);
  const [pick, setPick] = useState<[string, string[]] | null>(null);
  const cats = useMemo(() => hierarchy(filaments), [filaments]);

  if (!object)
    return <div className="obj-settings-empty">Select an object in the list or on the plate to edit its settings.</div>;
  if (object.project)
    return (
      <div className="obj-settings-empty">
        {object.name} belongs to the opened 3MF project; its per-object settings travel inside the project file.
      </div>
    );

  const add = (keys: string[], remove: string[] = []) => {
    for (const k of keys) if (!(k in object.settings)) setObjectOption(object.id, k, global[k] ?? (OPTIONS[k]?.default as ConfigValue) ?? '');
    if (remove.length) removeObjectOptions(object.id, remove);
  };
  const byCat = new Map<string, string[]>();
  for (const k of Object.keys(object.settings)) {
    const cat = OPTIONS[k]?.category || 'Others';
    if (!byCat.has(cat)) byCat.set(cat, []);
    byCat.get(cat)!.push(k);
  }

  return (
    <div className="obj-settings">
      <div className="obj-settings-head">
        <Icon name="obj_printable" size={14} />
        <span className="ol-text" title={object.name}>
          {object.name}
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
            title="Delete all settings of this object"
            disabled={!Object.keys(object.settings).length}
            onClick={() => removeObjectOptions(object.id, Object.keys(object.settings))}
          />
        </span>
      </div>
      {!Object.keys(object.settings).length && (
        <div className="obj-settings-empty">This object uses the global process settings. Add settings with “+”.</div>
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
                value={object.settings[k]}
                systemValue={global[k]}
                dirty={asText(object.settings[k]) !== asText(global[k])}
                onSet={(key, v) => setObjectOption(object.id, key, v)}
                onReset={(key) => setObjectOption(object.id, key, global[key] ?? object.settings[key])}
                resetTitle="Reset to the global process value"
                extra={
                  <button type="button" className="icon-btn opt-remove" title="Remove this setting from the object" onClick={() => removeObjectOptions(object.id, [k])}>
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
          current={object.settings}
          onClose={() => setPick(null)}
          onOk={(checked) => {
            add(checked, pick[1].filter((k) => k in object.settings && !checked.includes(k)));
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
    <span className="menu" ref={ref}>
      <IconButton icon="add" title="Add settings" onClick={() => props.setOpen(!props.open)} />
      {props.open && (
        <div className="menu-pop menu-pop-right">
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
    <div className="modal-backdrop" onMouseDown={(e) => e.target === e.currentTarget && props.onClose()}>
      <div className="modal small" role="dialog" aria-label="Select settings">
        <div className="modal-title">
          <span>Select settings — {props.title}</span>
          <button className="modal-close" title="Close" onClick={props.onClose}>
            ×
          </button>
        </div>
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
      </div>
    </div>
  );
}
