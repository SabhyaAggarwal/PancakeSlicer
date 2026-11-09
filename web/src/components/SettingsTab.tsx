import { FilamentNozzleAssignment, VariantSelector } from '../multinozzle/NozzleControls';
import { ExtruderRows } from '../profiles/ExtruderRows';
import { optionIndex, selectedOptionValues, type ExtruderSelection } from '../multinozzle/config';
import { PresetBar } from '../profiles/PresetBar';
import { methodModes, RETIRED_GROUPS, SurfaceSettings, TOP_METHOD_KEYS } from './SurfaceSettings';
import { methodAvailability } from '../profiles/method-context';
import { isMethodSettingSupported } from '../profiles/capabilities';
import { NozzleField, PlateTypeField } from '../profiles/SetupRows';
import { Tabs } from 'radix-ui';
import { Suspense, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { preloadable } from '../preloadable';
import type { Config, ConfigValue, PresetType } from '../api/types';
import { isHidden, modeVisible, OPTIONS, splitKey, loadTab, useTab, type TabDesc, type TabLine, type OptionDef, type OptionMode } from '../options';
import { effectiveConfig, effectiveMode, getState, presetEdits, resetOption, resolvePreset, setOption, setState, useStore } from '../state/store';
import { condition, environment, fieldDefinition, optionState } from '../visibility';
import { hiddenReason } from '../settings/common';
import { lineMode, nativeSupportCapable, tabEnvironment } from '../settings-env';
import { Field } from './Field';
import { useTabFit } from './tab-rows';
import { LabelTip } from './InfoTip';
import { displayLabel } from './fields/labels';
import { GroupHead, isGroupOpen, setGroupOpen, useSettingsGroups } from './SettingsGroup';
import type { SettingSource } from '../profiles/bases';
import type { ObjectOverrides } from './ObjectOverrides';
import './settings.css';

const BedShapeDialog = preloadable(() => import('./BedShapeDialog'));
const HeadPicker = preloadable(() => import('./HeadPicker'));
const HeadShapeButton = preloadable(() => import('./HeadPicker').then((m) => ({ default: m.HeadShapeButton })));
const GcodeWorkbench = preloadable(() => import('../gcode/GcodeWorkbench'), { idle: false });
const isGcode = (key: string) => /_gcode$/.test(key) && (!!OPTIONS[key]?.multiline || OPTIONS[key]?.type === 'string' || OPTIONS[key]?.type === 'strings');

const customRow = (line: TabLine) => line.label === 'Head shape' && !line.options.length;
const groupName = (s: string) => s.toLowerCase().replace(/\b(enable|enabled|use)\b/g, '').replace(/\s+/g, ' ').trim();
const groupSwitch = (g: { title: string; lines: TabLine[] }) => !g.title ? -1 : g.lines.findIndex((l) => l.options.length === 1 && !l.index_labels && !l.indexed_columns
  && OPTIONS[splitKey(l.options[0])[0]]?.type === 'bool' && groupName(l.label ?? OPTIONS[splitKey(l.options[0])[0]]?.label ?? '') === groupName(g.title));

const TAB_OF: Record<PresetType, TabDesc['tab']> = { process: 'process', filament: 'filament', machine: 'printer' };
const TYPE_NAME: Record<PresetType, string> = { process: 'Process', filament: 'Filament', machine: 'Printer' };

export interface ProfileFields { readOnlyKeys?: ReadonlySet<string>; name: string; config: Config; baseline: Config; changes: Config; mode?: OptionMode; set(key: string, value: ConfigValue): void; reset(key: string): void;
  sources?: Record<string, SettingSource>; sourceText?: Record<string, string>; resetToBase?(key: string): void }
export interface Aux { label: string; icon?: string; content: React.ReactNode; modified?: boolean; initial?: boolean }
export function SettingsTab({ type, slot = 0, compact, hidePreset, editor, overrides, readOnly = false, auxiliary }: { type: PresetType; slot?: number; compact?: boolean; hidePreset?: boolean; editor?: ProfileFields;
  overrides?: ObjectOverrides | null; readOnly?: boolean; auxiliary?: Aux | Aux[] }) {
  const { desc, error } = useTab(TAB_OF[type]);
  const [retrying, setRetrying] = useState(false);
  const [retryError, setRetryError] = useState<string | null>(null);
  const retry = () => {
    setRetrying(true); setRetryError(null);
    void loadTab(TAB_OF[type]).catch(e => setRetryError(String(e))).finally(() => setRetrying(false));
  };
  const [bedDialog, setBedDialog] = useState(false);
  const [pageIdx, setPageIdx] = useState(0);
  const [extruderSelection, setExtruderSelection] = useState<ExtruderSelection>({ extruder: 0, variant: 0 });
  const auxes = auxiliary ? [auxiliary].flat() : [];
  const [auxIdx, setAuxIdx] = useState<number | null>(() => { const i = auxes.findIndex((a) => a.initial); return i < 0 ? null : i; });
  const aux = auxIdx !== null ? auxes[auxIdx] : undefined, auxiliaryOn = !!aux;
  const machineConfig = useStore((s) => effectiveConfig(s, 'machine'));
  const processConfig = useStore((s) => effectiveConfig(s, 'process'));
  const features = useStore((s) => s.features);
  const printer = useStore((s) => s.printer);
  const bedType = useStore((s) => s.bedType);
  const projectMode = useStore(effectiveMode);
  const uiMode = editor?.mode ?? projectMode;
  const projectDirty = useStore((s) => presetEdits(s, type, slot));
  const fields = editor ?? overrides ?? undefined;
  const dirty = fields?.changes ?? projectDirty;
  const presetName = useStore((s) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]));
  const userPresets = useStore((s) => s.userPresets);
  const machines = useStore(s => s.machines), processes = useStore(s => s.processes), filaments = useStore(s => s.filaments);
  const projectConfig = useStore(s => effectiveConfig(s, type, slot));
  const config = fields?.config ?? projectConfig;
  const unedited = useMemo(() => {
    const s = getState();
    return effectiveConfig(type === 'filament' ? {...s, filamentEdits: {...s.filamentEdits, [presetName ?? '']: {}}} : {...s, dirty: {...s.dirty, [type]: {}}}, type, slot);
  }, [projectConfig, type, slot, presetName]);
  const system = fields?.baseline ?? unedited;

  const env = useMemo(() => {
    const state = getState();
    if (overrides) return environment([machineConfig, config], { printer_vendor: resolvePreset(state, 'machine', state.printer).base?.vendor,
      extruders_count: Array.isArray(machineConfig.nozzle_diameter) ? machineConfig.nozzle_diameter.length : 1, curr_bed_type: state.bedType,
      settings_scope: 'object', native_support_capable: nativeSupportCapable(state) }, uiMode);
    if (!editor) {
      const context = tabEnvironment(state, type, slot, uiMode);
      return environment([machineConfig, processConfig, selectedOptionValues(config, type, extruderSelection)], {
        printer_vendor: context.printer_vendor, extruders_count: context.extruders_count, curr_bed_type: context.curr_bed_type,
        settings_scope: 'global', native_support_capable: context.native_support_capable,
      }, uiMode);
    }
    const machine = type === 'machine' ? config : machineConfig;
    const base = resolvePreset(state, type, editor.name).base;
    return environment([machine, processConfig, selectedOptionValues(config, type, extruderSelection)], {printer_vendor: type === 'machine' ? base?.vendor : resolvePreset(state, 'machine', state.printer).base?.vendor,
      extruders_count: Array.isArray(machine.nozzle_diameter) ? machine.nozzle_diameter.length : 1, curr_bed_type: state.bedType, settings_scope: 'global', native_support_capable: type === 'machine' ? !!state.features.native_support && !!base?.native_support_capable : nativeSupportCapable(state)}, uiMode);
  }, [config, machineConfig, processConfig, features, printer, bedType, uiMode, editor?.name, !!overrides, type, slot, machines, processes, filaments, extruderSelection]);
  const availability = useMemo(() => methodAvailability(getState(), type === 'machine' && editor ? config : machineConfig), [machineConfig,config,type,!!editor,features,machines,userPresets]);
  const states = useMemo(() => desc ? optionState(desc, env) : {}, [desc, env]);
  const fieldVisible = (k: string, line: TabLine) => {
    const key = splitKey(k)[0], rank = lineMode(line, k);
    return OPTIONS[key] && rank && !isHidden(key) && modeVisible(rank, uiMode) && (type === 'machine' || hiddenReason(k, getState(), type, slot) !== 'not applicable') && (overrides ? overrides.keys.has(key) : type !== 'process' || !TOP_METHOD_KEYS.has(key)) && isMethodSettingSupported(key,config,availability);
  };
  const focusKey = useStore((s) => (s.settingsFocus?.type === type ? s.settingsFocus.key : null));
  const pages = useMemo(() => (desc?.pages ?? []).filter((p) => !p.condition || condition(p.condition, env) !== false)
    .map((p) => ({ ...p, groups: p.groups.filter((g) => (!g.condition || condition(g.condition, env) !== false) && !(type === 'process' && !overrides && RETIRED_GROUPS.has(g.title)))
      .map((g) => ({ ...g, lines: g.lines.filter((line) =>
        line.options.some((k) => splitKey(k)[0] === focusKey) || !line.options.some((k) => states[splitKey(k)[0]]?.visible === false) && !(line.options.length && line.options.every((k) => states[splitKey(k)[0]]?.enabled === false)))
        .map((line) => ({ ...line, options: line.options.filter((k) => fieldVisible(k, line)) }))
        .filter((line, _, all) => line.options.length || (customRow(line) && all.some((l) => l.options.includes('nonplanar_head')))) }))
      .filter((g) => g.lines.length) }))
    .filter((p) => p.groups.length), [desc, env, states, uiMode, config, availability, type, focusKey, overrides?.keys]);
  const legacyKeys = useMemo(() => type !== 'process' || overrides ? [] : (desc?.pages ?? []).flatMap((p) => p.groups).filter((g) => RETIRED_GROUPS.has(g.title))
    .flatMap((g) => g.lines.filter((line) => !line.options.some((k) => states[splitKey(k)[0]]?.visible === false)).flatMap((line) => line.options.filter((k) => {
      const key = splitKey(k)[0], rank = lineMode(line, k);
      return OPTIONS[key] && rank && !isHidden(key) && modeVisible(rank, uiMode) && isMethodSettingSupported(key, config, availability);
    }).map((k) => splitKey(k)[0]))), [desc, states, uiMode, config, availability, type, !!overrides]);
  const page = pages[Math.min(pageIdx, pages.length - 1)];
  const gcodeKeys = page ? page.groups.flatMap((g) => g.lines.flatMap((l) => l.options.map((k) => splitKey(k)[0]))).filter(isGcode) : [];
  const gcodeLabels = Object.fromEntries((page?.groups ?? []).flatMap((g) => g.lines.flatMap((l) => l.options.map((k) => splitKey(k)[0]).filter(isGcode).map((k) => [k, (g.lines.length === 1 && g.title) || l.label || OPTIONS[k]?.label || k]))));
  const groups = (page?.groups ?? []).map((g) => gcodeKeys.length ? { ...g, lines: g.lines.filter((l) => !l.options.every((k) => isGcode(splitKey(k)[0]))) } : g).filter((g) => g.lines.length);
  const definitionCache = useRef(new Map<string, Partial<OptionDef>>());
  const definition = (key: string, line: TabLine) => {
    const next = {...fieldDefinition(key, env), ...line.overrides?.[key], ...key === 'nonplanar_mode' && methodModes(String(config[key] ?? 'off'), availability)}, prior = definitionCache.current.get(key);
    const same = prior && Object.keys(prior).length === Object.keys(next).length && Object.entries(next).every(([k,v]) => {
      const old = prior[k as keyof OptionDef]; return Array.isArray(v) && Array.isArray(old) ? v.length === old.length && v.every((x,i) => x === old[i]) : v === old;
    });
    if (same) return prior;
    definitionCache.current.set(key, next); return next;
  };
  const groupsVersion = useSettingsGroups();
  const groupId = (pageTitle: string, groupTitle: string) => `${TAB_OF[type]}/${pageTitle}/${groupTitle}`;
  const pageModified = (p: TabDesc['pages'][number]) => p.groups.some((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] in dirty)));
  const focus = useStore((s) => (s.settingsFocus?.type === type ? s.settingsFocus : null));
  const strip = useRef<HTMLDivElement>(null);
  const breaks = useTabFit(strip, `${pages.map((p) => p.title).join('|')}|${auxes.map((a) => a.label).join('|')}`);
  const openByDefault = editor ? typeof window !== 'undefined' && window.innerWidth >= 1100 : uiMode === 'simple';
  const body = useRef<HTMLDivElement>(null);
  const labelWidths = useRef<{ flow: Element | null; byKey: Map<string, number> }>({ flow: null, byKey: new Map() });
  const rowKeys = groups.flatMap((g) => g.lines.flatMap((l) => l.options)).join(',');
  useLayoutEffect(() => {
    const flow = editor ? body.current?.querySelector<HTMLElement>('.page-flow') : null;
    if (!flow) return;
    const cache = labelWidths.current;
    if (cache.flow !== flow) { cache.flow = flow; cache.byKey.clear(); }
    const natural = (l: Element, extra: number) => {
      const row = l.parentElement, id = `${row?.dataset.opt ?? row?.querySelector('[data-opt]')?.getAttribute('data-opt') ?? ''}|${l.textContent}`;
      let w = cache.byKey.get(id);
      if (w === undefined) { w = [...l.children].reduce((n, c) => n + (c as HTMLElement).scrollWidth, 0) + extra; cache.byKey.set(id, w); }
      return w;
    };
    const widest = Math.max(0, ...[...flow.querySelectorAll('.opt-line:not(.full) > .opt-label')].map((l) => natural(l, 0)), ...[...flow.querySelectorAll('.option-row > .opt-label')].map((l) => natural(l, 80)));
    flow.style.setProperty('--label-w', `${Math.min(240, Math.ceil(widest) + 4)}px`);
  }, [!!editor, rowKeys, auxiliaryOn, groupsVersion]);
  useEffect(() => {
    if (!focus || !desc) return;
    const i = pages.findIndex((p) => p.title === focus.page);
    if (i >= 0) setPageIdx(i);
    const holder = pages[i]?.groups.find((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] === focus.key)));
    if (holder) setGroupOpen(groupId(pages[i].title, holder.title), true);
    let tries = 0, timer: ReturnType<typeof setTimeout>;
    const reveal = () => {
      const line = body.current?.querySelector<HTMLElement>(`[data-opt="${CSS.escape(focus.key)}"]`);
      if (!line) { if (++tries < 30) timer = setTimeout(reveal, 33); return; }
      line.scrollIntoView({ block: 'center' });
      line.classList.remove('search-flash');
      void line.offsetWidth;
      line.classList.add('search-flash');
      const control = 'input:not([type=checkbox].nullable-toggle),select,textarea,button';
      (line.querySelector<HTMLElement>(`.opt-field :is(${control})`) ?? line.querySelector<HTMLElement>(control))?.focus({ preventScroll: true });
    };
    timer = setTimeout(reveal, 30);
    return () => clearTimeout(timer);
  }, [focus?.nonce, !!desc]);
  return (
    <div className={`settings-tab ${compact ? 'compact' : ''}`}>
      {bedDialog && <Suspense fallback={<div role="status">Loading bed shape…</div>}><BedShapeDialog config={config} onSet={editor?.set} close={() => setBedDialog(false)} /></Suspense>}
      {!hidePreset && <PresetBar type={type} slot={slot} />}
      <Tabs.Root className="settings-pages" value={auxiliaryOn ? `aux:${auxIdx}` : String(Math.min(pageIdx, pages.length - 1))} onValueChange={v => {const a = v.startsWith('aux:') ? Number(v.slice(4)) : null; setAuxIdx(a); if (a === null) setPageIdx(Number(v));}}><Tabs.List ref={strip} className="page-bar" aria-label={`${TYPE_NAME[type]} settings`}>
        {pages.map((p, i) => [breaks.includes(i) && <span key={`break-${i}`} className="tab-break" aria-hidden />, (
          <Tabs.Trigger key={p.title} value={String(i)} aria-label={p.title} className={`page-tab ${p === page && !auxiliaryOn ? 'active' : ''} ${pageModified(p) ? 'modified' : ''}`} onClick={() => setPageIdx(i)}>
            <span className="page-tab-label">{p.title}</span>
          </Tabs.Trigger>
        )])}
        {auxes.map((a, i) => [breaks.includes(pages.length + i) && <span key={`break-aux-${i}`} className="tab-break" aria-hidden />,
          <Tabs.Trigger key={`aux-${i}`} value={`aux:${i}`} className={`page-tab ${auxIdx === i ? 'active' : ''} ${a.modified ? 'modified' : ''}`}><span className="page-tab-label">{a.label}</span></Tabs.Trigger>])}
      </Tabs.List>
      <Tabs.Content forceMount value={auxiliaryOn ? `aux:${auxIdx}` : String(Math.min(pageIdx,pages.length-1))} className="page-body" ref={body}>
        {aux ? aux.content : <><div className="page-flow">
        {!overrides && <VariantSelector type={type} config={config} machine={type === 'machine' ? config : machineConfig} selection={extruderSelection} onChange={setExtruderSelection} />}
        {type === 'machine' && !editor && <><NozzleField /><PlateTypeField /></>}
        {type === 'machine' && !overrides && <ExtruderRows config={editor ? config : undefined} onSet={editor?.set} disabled={readOnly} />}
        {type === 'filament' && !fields && <FilamentNozzleAssignment slot={slot} disabled={readOnly} />}
        {type === 'process' && pageIdx === 0 && !overrides && <SurfaceSettings config={config} system={system} availability={availability} editor={editor} dirty={dirty} uiMode={uiMode} legacyKeys={legacyKeys} readOnly={readOnly}/>}
        {!desc && <div className="settings-load" role={error || retryError ? 'alert' : 'status'}><span>{retryError || error ? `Settings failed to load: ${retryError ?? error}` : 'Loading settings…'}</span>{(error || retryError) && <button type="button" className="btn" disabled={retrying} onClick={retry}>{retrying ? 'Retrying…' : 'Retry'}</button>}</div>}
        {type === 'process' && !overrides && page?.title === 'Multimaterial' && getState().filamentSlots.length > 1 && <section className="optgroup"><button type="button" className="btn" onClick={() => setState({flushDialog: true})}>Flush volumes</button></section>}
        {groups.map((g) => {
          const single = groups.length === 1 && g.lines.length === 1 && g.lines[0].options.length === 1 && !!OPTIONS[splitKey(g.lines[0].options[0])[0]]?.multiline, id = groupId(page.title, g.title), open = !g.title || single || isGroupOpen(id, openByDefault);
          const modified = g.lines.some((l) => l.options.some((k) => splitKey(k)[0] in dirty));
          const method = type === 'process' ? String(config.nonplanar_mode ?? OPTIONS.nonplanar_mode?.default ?? 'off') : undefined;
          const field = (line: TabLine, keys: string[], k: string, i: number, labels: string[] | undefined, shared: boolean, tooltip: string | undefined, hide = false) => {
            const [key, column] = splitKey(k);
            const index = !overrides && !line.index_labels ? optionIndex(key, type, config, extruderSelection, column) : null;
            const indexedKey = index === null ? k : `${key}#${index}`;
            const source = editor?.sources?.[key];
            return <Field widget={type === 'machine' && key === 'printable_area' ? <button type="button" className="btn" onClick={() => setBedDialog(true)}>Set bed shape</button>
              : type === 'machine' && key === 'nonplanar_head' ? <Suspense fallback={null}><HeadPicker value={String(config[key] ?? '')} onCommit={(v) => editor ? editor.set(key, v) : v === String(system[key] ?? '') ? resetOption(type, key) : setOption(type, key, v)} /></Suspense>
              : undefined} key={`${k}:${i}`} optKey={indexedKey} type={type} value={config[key]} systemValue={system[key]} dirty={key in dirty}
              slot={slot} onSet={fields?.set} onReset={fields?.reset} mixed={overrides?.mixed[key]} resetTitle={overrides ? 'Reset to project' : undefined} mode={method}
              source={source} sourceText={editor?.sourceText?.[key]} onResetToBase={source === 'set' && !readOnly ? editor?.resetToBase : undefined}
              disabled={readOnly || editor?.readOnlyKeys?.has(key) || states[key]?.enabled === false}
              definition={definition(key, line)}
              inheritedValue={env[key.replace(/^filament_/, '')] as ConfigValue}
              overrideDisabled={readOnly || OPTIONS[key]?.nullable && key !== 'filament_retraction_length' && !key.startsWith('filament_ironing_') && env.filament_retraction_length != null && Number(env.filament_retraction_length) <= 0}
              label={labels?.[i] ?? (shared ? line.overrides?.[key]?.label ?? OPTIONS[key]?.label : line.label)}
              hideLabel={hide || g.lines.length === 1 && keys.length === 1 && !!OPTIONS[key]?.multiline && (line.label ?? OPTIONS[key]?.label ?? '').trim().toLowerCase() === g.title.trim().toLowerCase()}
              tooltip={tooltip} />;
          };
          const own = groupSwitch(g), ownLine = own >= 0 ? g.lines[own] : undefined, fixed = !!ownLine && g.lines.length === 1;
          const sub = (l: TabLine, k: string) => l.overrides?.[splitKey(k)[0]]?.label ?? OPTIONS[splitKey(k)[0]]?.label ?? '';
          const pairable = g.lines.length > 0 && own < 0 && g.lines.every((l) => l.options.length === 2 && !l.index_labels && !l.indexed_columns && !customRow(l));
          const pairs = pairable && sub(g.lines[0], g.lines[0].options[0]) && g.lines.every((l) => sub(l, l.options[0]) === sub(g.lines[0], g.lines[0].options[0]) && sub(l, l.options[1]) === sub(g.lines[0], g.lines[0].options[1]))
            ? [sub(g.lines[0], g.lines[0].options[0]), sub(g.lines[0], g.lines[0].options[1])] : null;
          return <section key={g.title} className="optgroup" data-collapsed={open || fixed ? undefined : ''} data-single={single ? '' : undefined} data-pairs={pairs ? '' : undefined} data-lone={fixed ? '' : undefined}>
            {g.title && !single && !fixed && <GroupHead id={id} title={g.title} modified={modified} open={open} aside={ownLine && field(ownLine, ownLine.options, ownLine.options[0], 0, undefined, false, ownLine.tooltip && OPTIONS[ownLine.tooltip] ? OPTIONS[ownLine.tooltip].tooltip : ownLine.tooltip, true)} />}
            {open && pairs && <div className="opt-pair-head" aria-hidden><span>{pairs[0]}</span><span>{pairs[1]}</span></div>}
            {(open || fixed) && g.lines.map((line, row) => {
              if (row === own && !fixed) return null;
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
                {shared && <span className="opt-label"><LabelTip label={sharedLabel && displayLabel(splitKey(keys[0])[0], sharedLabel)} full={sharedLabel}><strong>{sharedLabel}</strong>{tooltip && <p>{tooltip}</p>}</LabelTip></span>}
                {keys.map((k, i) => field(line, keys, k, i, labels, shared, tooltip))}
              </div>;
            })}
          </section>;
        })}</div>
        {gcodeKeys.length > 0 && <Suspense fallback={<div className={`gcode-bench ${groups.length ? '' : 'fill'}`} role="status" aria-label="Loading G-code editor" />}>
          <GcodeWorkbench type={type} slot={slot} keys={gcodeKeys} labels={gcodeLabels} config={config} system={system} dirty={dirty} editor={editor} readOnly={readOnly} fill={!groups.length} />
        </Suspense>}
        </>}
      </Tabs.Content></Tabs.Root>
    </div>
  );
}
