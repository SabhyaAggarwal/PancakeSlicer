import { PresetBar } from '../profiles/PresetBar';
import { SurfaceSettings, TOP_METHOD_KEYS } from './SurfaceSettings';
import { methodAvailability } from '../profiles/method-context';
import { isMethodSettingSupported } from '../profiles/capabilities';
import { NozzleField } from '../profiles/SetupRows';
import { Tabs } from 'radix-ui';
import { lazy, Suspense, useEffect, useMemo, useRef, useState } from 'react';
import type { Config, ConfigValue, PresetType } from '../api/types';
import { isDeferred, isHidden, modeVisible, OPTIONS, splitKey, loadTab, useTab, type TabDesc, type TabLine, type OptionDef, type OptionMode } from '../options';
import { effectiveConfig, effectiveMode, getState, presetEdits, resetOption, resolvePreset, setOption, setState, useStore } from '../state/store';
import { condition, environment, fieldDefinition, optionState } from '../visibility';
import { lineMode, nativeSupportCapable, tabEnvironment } from '../settings-env';
import { Field } from './Field';
import { useTabFit } from './tab-rows';
import { LabelTip } from './InfoTip';
import { Icon } from './Icon';
import { GroupHead, isGroupOpen, setGroupOpen, useSettingsGroups } from './SettingsGroup';
import './settings.css';

const BedShapeDialog = lazy(() => import('./BedShapeDialog'));
const HeadPicker = lazy(() => import('./HeadPicker'));
const HeadShapeButton = lazy(() => import('./HeadPicker').then((m) => ({ default: m.HeadShapeButton })));
const GcodeWorkbench = lazy(() => import('../gcode/GcodeWorkbench'));
const isGcode = (key: string) => /_gcode$/.test(key) && (!!OPTIONS[key]?.multiline || OPTIONS[key]?.type === 'string' || OPTIONS[key]?.type === 'strings');

const customRow = (line: TabLine) => line.label === 'Head shape' && !line.options.length;
const groupName = (s: string) => s.toLowerCase().replace(/\b(enable|enabled|use)\b/g, '').replace(/\s+/g, ' ').trim();
const groupSwitch = (g: { title: string; lines: TabLine[] }) => !g.title ? -1 : g.lines.findIndex((l) => l.options.length === 1 && !l.index_labels && !l.indexed_columns
  && OPTIONS[splitKey(l.options[0])[0]]?.type === 'bool' && groupName(l.label ?? OPTIONS[splitKey(l.options[0])[0]]?.label ?? '') === groupName(g.title));

const TAB_OF: Record<PresetType, TabDesc['tab']> = { process: 'process', filament: 'filament', machine: 'printer' };
const TYPE_NAME: Record<PresetType, string> = { process: 'Process', filament: 'Filament', machine: 'Printer' };

export interface ProfileFields { name: string; config: Config; baseline: Config; changes: Config; mode?: OptionMode; set(key: string, value: ConfigValue): void; reset(key: string): void }
export function SettingsTab({ type, slot = 0, compact, hidePreset, editor, readOnly = false, auxiliary }: { type: PresetType; slot?: number; compact?: boolean; hidePreset?: boolean; editor?: ProfileFields; readOnly?: boolean; auxiliary?: {label:string; icon?:string; content:React.ReactNode; modified?:boolean} }) {
  const { desc, error } = useTab(TAB_OF[type]);
  const [retrying, setRetrying] = useState(false);
  const [retryError, setRetryError] = useState<string | null>(null);
  const retry = () => {
    setRetrying(true); setRetryError(null);
    void loadTab(TAB_OF[type]).catch(e => setRetryError(String(e))).finally(() => setRetrying(false));
  };
  const [bedDialog, setBedDialog] = useState(false);
  const [pageIdx, setPageIdx] = useState(0);
  const [auxiliaryOn,setAuxiliaryOn] = useState(false);
  const machineConfig = useStore((s) => effectiveConfig(s, 'machine'));
  const processConfig = useStore((s) => effectiveConfig(s, 'process'));
  const features = useStore((s) => s.features);
  const printer = useStore((s) => s.printer);
  const bedType = useStore((s) => s.bedType);
  const projectMode = useStore(effectiveMode);
  const uiMode = editor?.mode ?? projectMode;
  const projectDirty = useStore((s) => presetEdits(s, type, slot));
  const dirty = editor?.changes ?? projectDirty;
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const userPresets = useStore((s) => s.userPresets);
  const machines = useStore(s => s.machines), processes = useStore(s => s.processes), filaments = useStore(s => s.filaments);
  const projectConfig = useStore(s => effectiveConfig(s, type, slot));
  const config = editor?.config ?? projectConfig;
  const savedConfig = useMemo(() => resolvePreset(getState(), type, presetName ?? '').config, [type, presetName, userPresets, machines, processes, filaments]);
  const system = editor?.baseline ?? savedConfig;

  const env = useMemo(() => {
    const state = getState();
    if (!editor) return tabEnvironment(state, type, slot, uiMode);
    const machine = type === 'machine' ? config : machineConfig;
    const base = resolvePreset(state, type, editor.name).base;
    return environment([machine, processConfig, config], {printer_vendor: type === 'machine' ? base?.vendor : resolvePreset(state, 'machine', state.printer).base?.vendor,
      extruders_count: Array.isArray(machine.nozzle_diameter) ? machine.nozzle_diameter.length : 1, curr_bed_type: state.bedType, settings_scope: 'global', native_support_capable: type === 'machine' ? !!state.features.native_support && !!base?.native_support_capable : nativeSupportCapable(state)}, uiMode);
  }, [config, machineConfig, processConfig, features, printer, bedType, uiMode, editor?.name, type, slot, machines, processes, filaments]);
  const availability = useMemo(() => methodAvailability(getState(), type === 'machine' && editor ? config : machineConfig), [machineConfig,config,type,!!editor,features,machines,userPresets]);
  const states = useMemo(() => desc ? optionState(desc, env) : {}, [desc, env]);
  const fieldVisible = (k: string, line: TabLine) => {
    const key = splitKey(k)[0], rank = lineMode(line, k);
    return OPTIONS[key] && rank && !isHidden(key) && modeVisible(rank, uiMode) && (type !== 'process' || !TOP_METHOD_KEYS.has(key)) && isMethodSettingSupported(key,config,availability);
  };
  const pages = useMemo(() => (desc?.pages ?? []).filter((p) => !p.condition || condition(p.condition, env) !== false)
    .map((p) => ({ ...p, groups: p.groups.filter((g) => !g.condition || condition(g.condition, env) !== false)
      .map((g) => ({ ...g, lines: g.lines.filter((line) =>
        !line.options.some((k) => states[splitKey(k)[0]]?.visible === false) && !(line.options.length && line.options.every((k) => states[splitKey(k)[0]]?.enabled === false)))
        .map((line) => ({ ...line, options: line.options.filter((k) => fieldVisible(k, line)) }))
        .filter((line, _, all) => line.options.length || (customRow(line) && all.some((l) => l.options.includes('nonplanar_head')))) }))
      .filter((g) => g.lines.length) }))
    .filter((p) => p.groups.length), [desc, env, states, uiMode, config, availability, type]);
  const page = pages[Math.min(pageIdx, pages.length - 1)];
  const gcodeKeys = page ? page.groups.flatMap((g) => g.lines.flatMap((l) => l.options.map((k) => splitKey(k)[0]))).filter(isGcode) : [];
  const gcodeLabels = Object.fromEntries((page?.groups ?? []).flatMap((g) => g.lines.flatMap((l) => l.options.map((k) => splitKey(k)[0]).filter(isGcode).map((k) => [k, (g.lines.length === 1 && g.title) || l.label || OPTIONS[k]?.label || k]))));
  const groups = (page?.groups ?? []).map((g) => gcodeKeys.length ? { ...g, lines: g.lines.filter((l) => !l.options.every((k) => isGcode(splitKey(k)[0]))) } : g).filter((g) => g.lines.length);
  const definitionCache = useRef(new Map<string, Partial<OptionDef>>());
  const definition = (key: string, line: TabLine) => {
    const next = {...fieldDefinition(key, env), ...line.overrides?.[key]}, prior = definitionCache.current.get(key);
    const same = prior && Object.keys(prior).length === Object.keys(next).length && Object.entries(next).every(([k,v]) => {
      const old = prior[k as keyof OptionDef]; return Array.isArray(v) && Array.isArray(old) ? v.length === old.length && v.every((x,i) => x === old[i]) : v === old;
    });
    if (same) return prior;
    definitionCache.current.set(key, next); return next;
  };
  useSettingsGroups();
  const groupId = (pageTitle: string, groupTitle: string) => `${TAB_OF[type]}/${pageTitle}/${groupTitle}`;
  const pageModified = (p: TabDesc['pages'][number]) => p.groups.some((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] in dirty)));
  const focus = useStore((s) => (s.settingsFocus?.type === type ? s.settingsFocus : null));
  const strip = useRef<HTMLDivElement>(null);
  const breaks = useTabFit(strip, `${pages.map((p) => p.title).join('|')}|${auxiliary?.label ?? ''}`);
  const body = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!focus || !desc) return;
    const i = pages.findIndex((p) => p.title === focus.page);
    if (i >= 0) setPageIdx(i);
    const holder = pages[i]?.groups.find((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] === focus.key)));
    if (holder) setGroupOpen(groupId(pages[i].title, holder.title), true);
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
      {bedDialog && <Suspense fallback={<div role="status">Loading bed shape…</div>}><BedShapeDialog config={config} onSet={editor?.set} close={() => setBedDialog(false)} /></Suspense>}
      {!hidePreset && <PresetBar type={type} slot={slot} />}
      <Tabs.Root className="settings-pages" value={auxiliaryOn ? 'auxiliary' : String(Math.min(pageIdx, pages.length - 1))} onValueChange={v => {setAuxiliaryOn(v === 'auxiliary'); if(v !== 'auxiliary') setPageIdx(Number(v));}}><Tabs.List ref={strip} className="page-bar" aria-label={`${TYPE_NAME[type]} settings`}>
        {pages.map((p, i) => [breaks.includes(i) && <span key={`break-${i}`} className="tab-break" aria-hidden />, (
          <Tabs.Trigger key={p.title} value={String(i)} aria-label={p.title} className={`page-tab ${p === page ? 'active' : ''} ${pageModified(p) ? 'modified' : ''}`} onClick={() => setPageIdx(i)}>
            {p.icon && <Icon name={p.icon} size={16}/>}<span className="page-tab-label">{p.title}</span>
          </Tabs.Trigger>
        )])}
        {auxiliary && breaks.includes(pages.length) && <span className="tab-break" aria-hidden />}
        {auxiliary && <Tabs.Trigger value="auxiliary" className={`page-tab ${auxiliaryOn ? 'active' : ''} ${auxiliary.modified ? 'modified' : ''}`}>{auxiliary.icon && <Icon name={auxiliary.icon} size={16}/>}<span className="page-tab-label">{auxiliary.label}</span></Tabs.Trigger>}
      </Tabs.List>
      <Tabs.Content forceMount value={auxiliaryOn ? 'auxiliary' : String(Math.min(pageIdx,pages.length-1))} className="page-body" ref={body}>
        {auxiliaryOn && auxiliary ? auxiliary.content : <>
        {type === 'machine' && !editor && <NozzleField />}
        {type === 'process' && pageIdx === 0 && <SurfaceSettings config={config} system={system} availability={availability} editor={editor} dirty={dirty} readOnly={readOnly}/>}
        {!desc && <div className="settings-load" role={error || retryError ? 'alert' : 'status'}><span>{retryError || error ? `Settings failed to load: ${retryError ?? error}` : 'Loading settings…'}</span>{(error || retryError) && <button type="button" className="btn" disabled={retrying} onClick={retry}>{retrying ? 'Retrying…' : 'Retry'}</button>}</div>}
        {type === 'process' && page?.title === 'Multimaterial' && getState().filamentSlots.length > 1 && <section className="optgroup"><button type="button" className="btn" onClick={() => setState({flushDialog: true})}>Flush volumes</button></section>}
        {groups.map((g) => {
          const single = groups.length === 1 && g.lines.length === 1 && g.lines[0].options.length === 1 && !!OPTIONS[splitKey(g.lines[0].options[0])[0]]?.multiline, id = groupId(page.title, g.title), open = !g.title || single || isGroupOpen(id);
          const modified = g.lines.some((l) => l.options.some((k) => splitKey(k)[0] in dirty));
          const field = (line: TabLine, keys: string[], k: string, i: number, labels: string[] | undefined, shared: boolean, tooltip: string | undefined, hide = false) => {
            const key = splitKey(k)[0];
            return <Field widget={type === 'machine' && key === 'printable_area' ? <button type="button" className="btn" onClick={() => setBedDialog(true)}>Set bed shape</button>
              : type === 'machine' && key === 'nonplanar_head' ? <Suspense fallback={null}><HeadPicker value={String(config[key] ?? '')} onCommit={(v) => editor ? editor.set(key, v) : v === String(system[key] ?? '') ? resetOption(type, key) : setOption(type, key, v)} /></Suspense>
              : undefined} key={`${k}:${i}`} optKey={k} type={type} value={config[key]} systemValue={system[key]} dirty={key in dirty}
              slot={slot} onSet={editor?.set} onReset={editor?.reset}
              disabled={readOnly || isDeferred(key) || states[key]?.enabled === false}
              definition={definition(key, line)}
              inheritedValue={env[key.replace(/^filament_/, '')] as ConfigValue}
              overrideDisabled={readOnly || OPTIONS[key]?.nullable && key !== 'filament_retraction_length' && !key.startsWith('filament_ironing_') && env.filament_retraction_length != null && Number(env.filament_retraction_length) <= 0}
              label={labels?.[i] ?? (shared ? line.overrides?.[key]?.label ?? OPTIONS[key]?.label : line.label)}
              hideLabel={hide || g.lines.length === 1 && keys.length === 1 && !!OPTIONS[key]?.multiline && (line.label ?? OPTIONS[key]?.label ?? '').trim().toLowerCase() === g.title.trim().toLowerCase()}
              tooltip={tooltip} />;
          };
          const own = groupSwitch(g), ownLine = own >= 0 ? g.lines[own] : undefined, fixed = !!ownLine && g.lines.length === 1;
          return <section key={g.title} className="optgroup" data-collapsed={open ? undefined : ''} data-single={single ? '' : undefined}>
            {g.title && !single && <GroupHead id={id} title={g.title} icon={g.icon} modified={modified} open={open || fixed} fixed={fixed} aside={ownLine && field(ownLine, ownLine.options, ownLine.options[0], 0, undefined, false, ownLine.tooltip && OPTIONS[ownLine.tooltip] ? OPTIONS[ownLine.tooltip].tooltip : ownLine.tooltip, true)} />}
            {open && g.lines.map((line, row) => {
              if (row === own) return null;
              let keys = line.options;
              let labels = line.index_labels;
              if (labels) keys = keys.flatMap((k) => labels!.map((_, i) => `${splitKey(k)[0]}#${i}`));
              if (line.indexed_columns) {
                labels = env.silent_mode ? ['Normal', 'Silent'] : undefined;
                keys = keys.flatMap((k) => (labels ?? ['']).map((_, i) => `${splitKey(k)[0]}#${i}`));
              }
              if (customRow(line)) return <div key={row} className="opt-line">
                <span className="opt-label"><LabelTip label={line.label} full={line.label}><strong>{line.label}</strong>{line.tooltip && <p>{line.tooltip}</p>}</LabelTip></span>
                <span className="opt-field"><Suspense fallback={null}><HeadShapeButton head={String(config.nonplanar_head ?? '')} layerHeight={Number(effectiveConfig(getState(), 'process').layer_height) || 0.2} onSaved={editor ? id => editor.set('nonplanar_head', id) : undefined} /></Suspense></span>
              </div>;
              const shared = keys.length > 1;
              const tooltip = line.tooltip && OPTIONS[line.tooltip] ? OPTIONS[line.tooltip].tooltip : line.tooltip;
              const sharedLabel = line.label ?? (OPTIONS[splitKey(keys[0])[0]]?.full_label || OPTIONS[splitKey(keys[0])[0]]?.label);
              return <div key={row} className={shared ? 'option-row' : undefined}>
                {shared && <span className="opt-label"><LabelTip label={sharedLabel} full={sharedLabel}><strong>{sharedLabel}</strong>{tooltip && <p>{tooltip}</p>}</LabelTip></span>}
                {keys.map((k, i) => field(line, keys, k, i, labels, shared, tooltip))}
              </div>;
            })}
          </section>;
        })}
        {gcodeKeys.length > 0 && <Suspense fallback={<div className={`gcode-bench ${groups.length ? '' : 'fill'}`} role="status" aria-label="Loading G-code editor" />}>
          <GcodeWorkbench type={type} slot={slot} keys={gcodeKeys} labels={gcodeLabels} config={config} system={system} dirty={dirty} editor={editor} readOnly={readOnly} fill={!groups.length} />
        </Suspense>}
        </>}
      </Tabs.Content></Tabs.Root>
    </div>
  );
}
