import { PresetBar } from '../profiles/PresetBar';
import { SurfaceSettings } from './SurfaceSettings';
import { NozzleField } from '../profiles/SetupRows';
import { Tabs } from 'radix-ui';
import { lazy, Suspense, useEffect, useMemo, useRef, useState } from 'react';
import type { ConfigValue, PresetType } from '../api/types';
import { isDeferred, isHidden, modeVisible, OPTIONS, splitKey, useTab, type TabDesc, type TabLine } from '../options';
import { effectiveConfig, effectiveMode, getState, presetEdits, resetOption, resolvePreset, setOption, useStore } from '../state/store';
import { condition, fieldDefinition, optionState } from '../visibility';
import { lineMode, tabEnvironment } from '../settings-env';
import { Field } from './Field';
import { Icon } from './Icon';

const BedShapeDialog = lazy(() => import('./BedShapeDialog'));
const HeadPicker = lazy(() => import('./HeadPicker'));
const HeadShapeButton = lazy(() => import('./HeadPicker').then((m) => ({ default: m.HeadShapeButton })));

const customRow = (line: TabLine) => line.label === 'Head shape' && !line.options.length;

const TAB_OF: Record<PresetType, TabDesc['tab']> = { process: 'process', filament: 'filament', machine: 'printer' };
const TYPE_NAME: Record<PresetType, string> = { process: 'Process', filament: 'Filament', machine: 'Printer' };

export function SettingsTab({ type, slot = 0, compact, hidePreset }: { type: PresetType; slot?: number; compact?: boolean; hidePreset?: boolean }) {
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
  const pageModified = (p: TabDesc['pages'][number]) => p.groups.some((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] in dirty)));
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
  return (
    <div className={`settings-tab ${compact ? 'compact' : ''}`}>
      {bedDialog && <Suspense fallback={<div role="status">Loading bed shape…</div>}><BedShapeDialog config={config} close={() => setBedDialog(false)} /></Suspense>}
      {!hidePreset && <PresetBar type={type} slot={slot} />}
      <Tabs.Root value={String(Math.min(pageIdx, pages.length - 1))} onValueChange={(v) => setPageIdx(Number(v))}><Tabs.List className="page-bar" aria-label={`${TYPE_NAME[type]} settings`}>
        {pages.map((p, i) => (
          <Tabs.Trigger key={p.title} value={String(i)} className={`page-tab ${p === page ? 'active' : ''} ${pageModified(p) ? 'modified' : ''}`} onClick={() => setPageIdx(i)}>
            {p.title}
          </Tabs.Trigger>
        ))}
      </Tabs.List></Tabs.Root>
      <div className="page-body" ref={body}>
        {type === 'machine' && <NozzleField />}
        {type === 'process' && pageIdx === 0 && <SurfaceSettings config={config} system={system} />}
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
