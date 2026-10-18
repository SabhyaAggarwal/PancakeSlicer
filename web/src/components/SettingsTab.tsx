import { useMemo, useState } from 'react';
import type { ConfigValue, PresetType } from '../api/types';
import { isDeferred, isHidden, modeVisible, OPTIONS, splitKey, useTab, type TabDesc, type TabLine } from '../options';
import { effectiveConfig, getState, resolvePreset, saveUserPreset, setState, toast, useStore } from '../state/store';
import { condition, environment, fieldDefinition, optionState } from '../visibility';
import { Field } from './Field';
import { Icon, IconButton } from './Icon';
import { PresetCombo } from './PresetCombo';

const TAB_OF: Record<PresetType, TabDesc['tab']> = { process: 'process', filament: 'filament', machine: 'printer' };

export function SettingsTab({ type, slot = 0, compact }: { type: PresetType; slot?: number; compact?: boolean }) {
  const { desc, error } = useTab(TAB_OF[type]);
  const [pageIdx, setPageIdx] = useState(0);
  const machineEdits = useStore((s) => s.dirty.machine);
  const printer = useStore((s) => s.printer);
  const bedType = useStore((s) => s.bedType);
  const uiMode = useStore((s) => s.uiMode);
  const dirty = useStore((s) => s.dirty[type]);
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const userPresets = useStore((s) => s.userPresets);
  const lists = useStore((s) => s.machines.length + s.processes.length + s.filaments.length);
  const config = useMemo(() => effectiveConfig(getState(), type, slot), [type, slot, presetName, dirty, userPresets, lists]);
  const system = useMemo(() => resolvePreset(getState(), type, presetName ?? '').config, [type, presetName, userPresets, lists]);

  const env = useMemo(() => {
    const state = getState();
    const machine = effectiveConfig(state, 'machine');
    const nozzles = machine.nozzle_diameter;
    return environment([machine, effectiveConfig(state, 'process'), config], {
      printer_vendor: resolvePreset(state, 'machine', state.printer).base?.vendor,
      extruders_count: Array.isArray(nozzles) ? nozzles.length : String(nozzles ?? '0.4').split(',').length,
      curr_bed_type: bedType, settings_scope: 'global',
    }, uiMode);
  }, [config, machineEdits, printer, bedType, uiMode]);
  const states = useMemo(() => desc ? optionState(desc, env) : {}, [desc, env]);
  const fieldVisible = (k: string, line: TabLine) => {
    const key = splitKey(k)[0], def = OPTIONS[key];
    const mode = line.overrides?.[key]?.mode as string | undefined;
    const rank = mode === 'Simple' ? 'simple' : mode === 'Advanced' ? 'advanced' : mode ? 'develop' : def?.mode;
    return def && !isHidden(key) && modeVisible(rank, uiMode);
  };
  const pages = (desc?.pages ?? []).filter((p) => !p.condition || condition(p.condition, env) !== false)
    .map((p) => ({ ...p, groups: p.groups.filter((g) => !g.condition || condition(g.condition, env) !== false)
      .map((g) => ({ ...g, lines: g.lines.filter((line) =>
        !line.options.some((k) => states[splitKey(k)[0]]?.visible === false))
        .map((line) => ({ ...line, options: line.options.filter((k) => fieldVisible(k, line)) }))
        .filter((line) => line.options.length) }))
      .filter((g) => g.lines.length) }))
    .filter((p) => p.groups.length);
  const page = pages[Math.min(pageIdx, pages.length - 1)];
  const isUser = userPresets.some((u) => u.type === type && u.name === presetName);
  const modifiedCount = Object.keys(dirty).length;

  const onSave = async () => {
    const suggested = isUser ? presetName : `${presetName} - Copy`;
    const name = window.prompt('Save preset as', suggested)?.trim();
    if (!name) return;
    if (getState()[type === 'machine' ? 'machines' : type === 'process' ? 'processes' : 'filaments'].some((p) => p.name === name)) {
      toast('A system preset has this name; choose another.');
      return;
    }
    await saveUserPreset(type, name);
    toast(`Preset "${name}" saved`);
  };
  const onDelete = async () => {
    const s = getState();
    if (!isUser || !s.api || !window.confirm(`Delete the preset "${presetName}"?`)) return;
    const { base } = resolvePreset(s, type, presetName!);
    await s.api.deleteUserPreset(type, presetName!);
    setState((st) => {
      const patch: Partial<typeof st> = { userPresets: st.userPresets.filter((u) => !(u.type === type && u.name === presetName)) };
      const fallback = base?.name ?? '';
      if (type === 'machine') patch.printer = fallback;
      else if (type === 'process') patch.process = fallback;
      else patch.filamentSlots = st.filamentSlots.map((f, i) => (i === slot ? fallback : f));
      return patch;
    });
  };

  return (
    <div className={`settings-tab ${compact ? 'compact' : ''}`}>
      <div className="tab-preset-row">
        <PresetCombo type={type} slot={slot} />
        <IconButton icon="save" title={modifiedCount ? `Save current ${type === 'machine' ? 'printer' : type} preset (${modifiedCount} modified)` : 'Save as user preset'} onClick={onSave} />
        <IconButton icon="delete" title="Delete this user preset" onClick={onDelete} disabled={!isUser} />
      </div>
      <div className="page-bar" role="tablist">
        {pages.map((p, i) => (
          <button key={p.title} role="tab" className={`page-tab ${p === page ? 'active' : ''}`} onClick={() => setPageIdx(i)}>
            {p.title}
          </button>
        ))}
      </div>
      <div className="page-body">
        {!desc && <div role="status">{error ? `Settings failed to load: ${error}` : 'Loading settings…'}</div>}
        {page?.groups.map((g) => (
          <section key={g.title} className="optgroup">
            {g.title && (
              <header className="optgroup-title">
                {g.icon && <Icon name={g.icon} size={16} />}
                <span>{g.title}</span>
              </header>
            )}
            {g.lines.map((line, row) => {
              let keys = line.options;
              let labels = line.index_labels;
              if (labels) keys = keys.flatMap((k) => labels!.map((_, i) => `${splitKey(k)[0]}#${i}`));
              if (line.indexed_columns) {
                labels = env.silent_mode ? ['Normal', 'Silent'] : ['Normal'];
                keys = keys.flatMap((k) => labels!.map((_, i) => `${splitKey(k)[0]}#${i}`));
              }
              const shared = keys.length > 1;
              return <div key={row} className={shared ? 'option-row' : undefined}>
                {shared && <span className="opt-label" title={line.tooltip}>{line.label ?? (OPTIONS[splitKey(keys[0])[0]]?.full_label || OPTIONS[splitKey(keys[0])[0]]?.label)}</span>}
                {keys.map((k, i) => {
                  const key = splitKey(k)[0];
                  return <Field key={`${k}:${i}`} optKey={k} type={type} value={config[key]} systemValue={system[key]} dirty={key in dirty}
                    disabled={isDeferred(key) || states[key]?.enabled === false}
                    definition={{ ...fieldDefinition(key, env), ...line.overrides?.[key] }}
                    inheritedValue={env[key.replace(/^filament_/, '')] as ConfigValue}
                    overrideDisabled={OPTIONS[key]?.nullable && key !== 'filament_retraction_length' && !key.startsWith('filament_ironing_') && env.filament_retraction_length != null && Number(env.filament_retraction_length) <= 0}
                    label={labels?.[i] ?? (shared ? line.overrides?.[key]?.label ?? OPTIONS[key]?.label : line.label)}
                    tooltip={line.tooltip} />;
                })}
              </div>;
            })}
          </section>
        ))}
      </div>
    </div>
  );
}
