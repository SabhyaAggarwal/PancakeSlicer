import { lazy, Suspense, useEffect, useMemo, useRef, useState } from 'react';
import { accessSnapshot } from '../api/access';
import type { ConfigValue, PresetType } from '../api/types';
import { isDeferred, isHidden, modeVisible, OPTIONS, splitKey, useTab, type TabDesc, type TabLine } from '../options';
import { effectiveConfig, effectiveMode, getState, presetEdits, resetOption, resolvePreset, setOption, setState, toast, useStore } from '../state/store';
import { condition, fieldDefinition, optionState } from '../visibility';
import { lineMode, tabEnvironment } from '../settings-env';
import { Field } from './Field';
import { Icon, IconButton } from './Icon';
import { PresetCombo } from './PresetCombo';
import { confirmDialog } from './Ask';

const BedShapeDialog = lazy(() => import('./BedShapeDialog'));
const PrintStyle = lazy(() => import('./PrintStyle'));
const HeadPicker = lazy(() => import('./HeadPicker'));
const HeadShapeButton = lazy(() => import('./HeadPicker').then((m) => ({ default: m.HeadShapeButton })));
const NonplanarSummary = lazy(() => import('./NonplanarSummary'));

async function exportCurrent(type: PresetType, slot: number) {
  const s = getState();
  const name = (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
  const { base, user } = resolvePreset(s, type, name);
  const project = type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type];
  const { exportPresetJson } = await import('../preset-io');
  exportPresetJson({ name, type, inherits: base?.name ?? name, overrides: { ...user?.overrides, ...project, ...presetEdits(s, type, slot) } });
}

const customRow = (line: TabLine) => line.label === 'Head shape' && !line.options.length;

const TAB_OF: Record<PresetType, TabDesc['tab']> = { process: 'process', filament: 'filament', machine: 'printer' };

export function SettingsTab({ type, slot = 0, compact }: { type: PresetType; slot?: number; compact?: boolean }) {
  const { desc, error } = useTab(TAB_OF[type]);
  const [bedDialog, setBedDialog] = useState(false);
  const [pageIdx, setPageIdx] = useState(0);
  const machineEdits = useStore((s) => s.dirty.machine);
  const printer = useStore((s) => s.printer);
  const bedType = useStore((s) => s.bedType);
  const uiMode = useStore(effectiveMode);
  const dirty = useStore((s) => presetEdits(s, type, slot));
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const userPresets = useStore((s) => s.userPresets);
  const lists = useStore((s) => s.machines.length + s.processes.length + s.filaments.length);
  const project = useStore((s) => s.projectPresets);
  const config = useMemo(() => effectiveConfig(getState(), type, slot), [type, slot, presetName, dirty, userPresets, lists, project]);
  const system = useMemo(() => resolvePreset(getState(), type, presetName ?? '').config, [type, presetName, userPresets, lists]);

  const env = useMemo(() => tabEnvironment(getState(), type, slot, uiMode), [config, machineEdits, printer, bedType, uiMode]);
  const states = useMemo(() => desc ? optionState(desc, env) : {}, [desc, env]);
  const fieldVisible = (k: string, line: TabLine) => {
    const key = splitKey(k)[0], rank = lineMode(line, k);
    return OPTIONS[key] && rank && !isHidden(key) && modeVisible(rank, uiMode);
  };
  const pages = (desc?.pages ?? []).filter((p) => !p.condition || condition(p.condition, env) !== false)
    .map((p) => ({ ...p, groups: p.groups.filter((g) => !g.condition || condition(g.condition, env) !== false)
      .map((g) => ({ ...g, lines: g.lines.filter((line) =>
        !line.options.some((k) => states[splitKey(k)[0]]?.visible === false))
        .map((line) => ({ ...line, options: line.options.filter((k) => fieldVisible(k, line)) }))
        .filter((line, _, all) => line.options.length || (customRow(line) && all.some((l) => l.options.includes('nonplanar_head')))) }))
      .filter((g) => g.lines.length) }))
    .filter((p) => p.groups.length);
  const page = pages[Math.min(pageIdx, pages.length - 1)];
  const focus = useStore((s) => (s.settingsFocus?.type === type ? s.settingsFocus : null));
  const body = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!focus || !desc) return;
    const i = pages.findIndex((p) => p.title === focus.page);
    if (i >= 0) setPageIdx(i);
    const timer = setTimeout(() => {
      const line = body.current?.querySelector<HTMLElement>(`[data-opt="${CSS.escape(focus.key)}"]`);
      if (!line) return;
      line.scrollIntoView({ block: 'center' });
      line.classList.remove('search-flash');
      void line.offsetWidth;
      line.classList.add('search-flash');
      line.querySelector<HTMLElement>('input:not([type=checkbox].nullable-toggle),select,textarea,button')?.focus({ preventScroll: true });
    }, 30);
    return () => clearTimeout(timer);
  }, [focus?.nonce, !!desc]);
  const isUser = userPresets.some((u) => u.type === type && u.name === presetName);
  const modifiedCount = Object.keys(dirty).length;

  const failed = (action: string, e: unknown) => {
    const message = e instanceof Error ? e.message : String(e);
    if (accessSnapshot()?.message !== message) toast(`${action} failed: ${message}`);
  };
  const onSave = () => setState({ presetUx: { kind: 'save', type, slot } });
  const onDelete = async () => {
    const s = getState();
    if (!isUser || !s.api || !(await confirmDialog('Delete Preset', `Delete the preset "${presetName}"?`))) return;
    const { base } = resolvePreset(s, type, presetName!);
    try {
      await s.api.deleteUserPreset(type, presetName!);
    } catch (e) {
      return failed('Deleting the preset', e);
    }
    setState((st) => {
      const patch: Partial<typeof st> = { userPresets: st.userPresets.filter((u) => !(u.type === type && u.name === presetName)) };
      const fallback = base?.name ?? '';
      if (type === 'machine') patch.printer = fallback;
      else if (type === 'process') patch.process = fallback;
      else patch.filamentSlots = st.filamentSlots.map((f) => (f === presetName ? fallback : f));
      return patch;
    });
  };

  return (
    <div className={`settings-tab ${compact ? 'compact' : ''}`}>
      {bedDialog && <Suspense fallback={<div role="status">Loading bed shape…</div>}><BedShapeDialog config={config} close={() => setBedDialog(false)} /></Suspense>}
      <div className="tab-preset-row">
        <PresetCombo type={type} slot={slot} />
        <IconButton icon="save" title={modifiedCount ? `Save current ${type === 'machine' ? 'printer' : type} preset (${modifiedCount} modified)` : 'Save as user preset'} onClick={onSave} />
        <IconButton icon="delete" title="Delete this user preset" onClick={onDelete} disabled={!isUser} />
        <IconButton icon="compare" title="Compare presets" onClick={() => setState({ presetUx: { kind: 'compare', type, left: presetName } })} />
        <IconButton icon="design_export" title="Export this preset, with its unsaved changes, as an OrcaSlicer preset file" onClick={() => void exportCurrent(type, slot)} />
        <IconButton icon="search" title="Search in preset (Ctrl+F)" onClick={() => setState({ presetUx: { kind: 'search' } })} />
      </div>
      {type === 'process' && compact && <Suspense fallback={null}><PrintStyle /></Suspense>}
      {type === 'process' && compact && <Suspense fallback={null}><NonplanarSummary /></Suspense>}
      <div className="page-bar" role="tablist">
        {pages.map((p, i) => (
          <button key={p.title} role="tab" className={`page-tab ${p === page ? 'active' : ''}`} onClick={() => setPageIdx(i)}>
            {p.title}
          </button>
        ))}
      </div>
      <div className="page-body" ref={body}>
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
                labels = env.silent_mode ? ['Normal', 'Silent'] : undefined;
                keys = keys.flatMap((k) => (labels ?? ['']).map((_, i) => `${splitKey(k)[0]}#${i}`));
              }
              if (customRow(line)) return <div key={row} className="opt-line">
                <span className="opt-label" title={line.tooltip}>{line.label}</span>
                <span className="opt-field"><Suspense fallback={null}><HeadShapeButton head={String(config.nonplanar_head ?? '')} layerHeight={Number(effectiveConfig(getState(), 'process').layer_height) || 0.2} /></Suspense></span>
              </div>;
              const shared = keys.length > 1;
              const tooltip = line.tooltip && OPTIONS[line.tooltip] ? OPTIONS[line.tooltip].tooltip : line.tooltip;
              return <div key={row} className={shared ? 'option-row' : undefined}>
                {shared && <span className="opt-label" title={tooltip}>{line.label ?? (OPTIONS[splitKey(keys[0])[0]]?.full_label || OPTIONS[splitKey(keys[0])[0]]?.label)}</span>}
                {keys.map((k, i) => {
                  const key = splitKey(k)[0];
                  return <Field widget={type === 'machine' && key === 'printable_area' ? <button className="side-btn" title="Set bed shape" onClick={() => setBedDialog(true)}>Set ...</button>
                    : type === 'machine' && key === 'nonplanar_head' ? <Suspense fallback={null}><HeadPicker value={String(config[key] ?? '')} onCommit={(v) => v === String(system[key] ?? '') ? resetOption(type, key) : setOption(type, key, v)} /></Suspense>
                    : undefined} key={`${k}:${i}`} optKey={k} type={type} value={config[key]} systemValue={system[key]} dirty={key in dirty}
                    slot={slot}
                    disabled={isDeferred(key) || states[key]?.enabled === false}
                    definition={{ ...fieldDefinition(key, env), ...line.overrides?.[key] }}
                    inheritedValue={env[key.replace(/^filament_/, '')] as ConfigValue}
                    overrideDisabled={OPTIONS[key]?.nullable && key !== 'filament_retraction_length' && !key.startsWith('filament_ironing_') && env.filament_retraction_length != null && Number(env.filament_retraction_length) <= 0}
                    label={labels?.[i] ?? (shared ? line.overrides?.[key]?.label ?? OPTIONS[key]?.label : line.label)}
                    tooltip={tooltip} />;
                })}
              </div>;
            })}
          </section>
        ))}
      </div>
    </div>
  );
}
