import { useMemo, type ReactNode } from 'react';
import { Popover } from 'radix-ui';
import { Caret } from '../components/Caret';
import type { Config, PresetType, UserPreset } from '../api/types';
import { Choice } from '../components/kit';
import { getState, useStore } from '../state/store';
import { selectNativeCompatibility } from './compatibility';
import { profileKey } from './identity';
import { modelLabel, vendorLabel } from './metadata';
import { findCatalogBase, resolveProfile } from './model';
import { fromApplicability1, matchesFilament, matchesPrint, matchesPrinter, type PrinterFacts, type WorksWithRule } from './works-with';



const first = (v: unknown) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');
const NOZZLES = [0.2, 0.25, 0.4, 0.6, 0.8];
const mm = (n: number) => `${n} mm`;
const plural = (n: number, one: string, many = `${one}s`) => `${n} ${n === 1 ? one : many}`;


export function worksWithRule(value: unknown): WorksWithRule | undefined {
  const v = value as {schema?: number; mode?: 'inherit' | 'only'; printerIds?: string[]} | undefined;
  return !v ? undefined : v.schema === 1 ? fromApplicability1(v as Parameters<typeof fromApplicability1>[0]) : value as WorksWithRule;
}

export function applicabilityKey(value: unknown): string {
  const r = worksWithRule(value);
  if (!r) return 'base';
  const sorted = (o: object | undefined) => o && Object.fromEntries(Object.entries(o).map(([k, v]) => [k, Array.isArray(v) ? [...v].sort() : v]).sort(([a], [b]) => a.localeCompare(b)));
  return JSON.stringify([sorted(r.printers), sorted(r.nozzle), sorted(r.filaments), sorted(r.prints), r.exclude && [...r.exclude].sort()]);
}

type Printer = PrinterFacts & {id: string; name: string; model: string; brand: string; own: boolean; label: string; detail: string};

function libraryFacts() {
  const s = getState(), live = s.userPresets.filter((p) => !p.archived);
  const printers: Printer[] = live.filter((p) => p.type === 'machine').map((p) => {
    const id = profileKey(p), c = resolveProfile(s, 'machine', id).config, vendor = findCatalogBase(s, 'machine', p.base ?? p.inherits ?? '').base?.vendor ?? '';
    const model = modelLabel(c, vendor), nozzle = Number(first(c.nozzle_diameter));
    return {id, name: p.name, printerModel: first(c.printer_model), model, brand: vendorLabel(c, vendor), own: p.kind !== 'master', nozzle, label: p.kind === 'master' ? [model, nozzle > 0 ? mm(nozzle) : ''].filter(Boolean).join(' · ') : p.name, detail: p.kind === 'master' ? '' : [model, nozzle > 0 ? mm(nozzle) : ''].filter(Boolean).join(' · ')};
  }).sort((a, b) => a.label.localeCompare(b.label));
  const named = (type: PresetType) => live.filter((p) => p.type === type && p.kind !== 'master').map((p) => ({id: profileKey(p), name: p.name, config: resolveProfile(s, type, profileKey(p)).config}))
    .sort((a, b) => a.name.localeCompare(b.name));
  const filaments = named('filament').map((f) => ({...f, type: first(f.config.filament_type)}));
  return {printers, filaments, prints: named('process'), current: s.printer};
}



export function worksWithNone(type: PresetType, value: unknown): boolean {
  const rule = worksWithRule(value);
  return type !== 'machine' && !!rule && !libraryFacts().printers.some((p) => matchesPrinter(rule, p));
}

const without = <K extends keyof WorksWithRule>(rule: WorksWithRule, key: K) => {const next = {...rule}; delete next[key]; return next;};
interface ChipOption<T> {id: T; label: string;                                                                          tip?: string;                                                                group?: string}

function Chips<T extends string | number>({label, options, value, onChange}: {label: string; options: ChipOption<T>[]; value: T[]; onChange(next: T[]): void}) {
  const chip = (o: ChipOption<T>) => {
    const on = value.includes(o.id), last = on && value.length === 1;
    return <button key={String(o.id)} type="button" className="chip" aria-pressed={on} aria-disabled={last || undefined} aria-description={o.tip}
      {...(o.tip ? {'data-truncated': 'true', 'data-full-text': o.tip} : {})}
      onClick={() => {if (!last) onChange(on ? value.filter((v) => v !== o.id) : [...value, o.id]);}}>{o.label}</button>;
  };
  const groups = [...new Set(options.map((o) => o.group ?? ''))];
  return groups.length < 2 ? <div className="chips ww-chips" role="group" aria-label={label}>{options.map(chip)}</div>
    : <div className="ww-chip-grid" role="group" aria-label={label}>{groups.map((g) => [<span key={`${g}:label`} className="ww-brand">{g}</span>,
      <div key={g} className="chips ww-chips">{options.filter((o) => (o.group ?? '') === g).map(chip)}</div>])}</div>;
}


function Modes({active, modes}: {active: string; modes: Record<string, ReactNode>}) {
  return <div className="ww-modes">{Object.entries(modes).map(([id, node]) => <div key={id} className="ww-mode" data-on={id === active || undefined} inert={id !== active || undefined}>{node}</div>)}</div>;
}


function Condition({label, summary, children}: {label: string; summary: string; children?: ReactNode}) {
  return <div className="ww-condition"><span className="ww-label">{label}</span>
    {children ? <Popover.Root><Popover.Trigger asChild><button type="button" className="dropdown ww-trigger" aria-label={`${label}: ${summary}`}><span className="dropdown-value">{summary}</span><Caret className="dropdown-caret"/></button></Popover.Trigger>
      <Popover.Portal><Popover.Content className="orca-menu ww-pop" aria-label={label} side="bottom" align="start" sideOffset={4} collisionPadding={12} onKeyDown={(e) => e.stopPropagation()}>{children}</Popover.Content></Popover.Portal></Popover.Root>
      : <span className="ww-value">{summary}</span>}
  </div>;
}



export function WorksWith({type, profile, source, config, value, onChange}: {type: PresetType; profile: string; source?: UserPreset; config: Config; value?: unknown; onChange?(value: WorksWithRule | undefined): void}) {
  const users = useStore((s) => s.userPresets), slots = useStore((s) => s.filamentSlots), process = useStore((s) => s.process);
  const facts = useMemo(libraryFacts, [users]);
  const stored = worksWithRule(value), key = profile;

  const native = useMemo(() => {
    if (stored) return [];
    const s = getState(), draft = source && {...source, overlay: config, overrides: config}, state = draft ? {...s, userPresets: s.userPresets.map((p) => profileKey(p) === key ? draft : p)} : s;
    return facts.printers.filter((p) => selectNativeCompatibility(state, type, key, p.id).ok);
  }, [stored, facts, config, source, type, key]);
  if (type === 'machine') return null;
  const {printers, filaments, prints} = facts, here = printers.find((p) => p.id === facts.current) ?? printers[0], mine = printers.filter((p) => p.own);


  const system = getState().machines, nozzlesOf = (model: string) => [...new Set([...system.filter((m) => first(m.config.printer_model) === model).map((m) => Number(first(m.config.nozzle_diameter))), ...printers.filter((p) => p.printerModel === model).map((p) => p.nozzle)].filter((n) => n > 0))].sort((a, b) => a - b);
  const models: ChipOption<string>[] = [...new Map(printers.filter((p) => p.printerModel).map((p) => [p.printerModel, p])).values()].map((p) => {
    const count = mine.filter((q) => q.printerModel === p.printerModel).length, n = nozzlesOf(p.printerModel);
    return {id: p.printerModel, label: p.model || p.printerModel, group: p.brand, tip: [p.printerModel, n.length ? `nozzles ${n.join('/')}` : '', `${count} of my printers`].filter(Boolean).join(' · ')};
  }).sort((a, b) => a.group!.localeCompare(b.group!) || a.label.localeCompare(b.label));

  const rule = stored ?? {schema: 2, ...(native.length ? {printers: {models: [...new Set(native.map((p) => p.printerModel).filter(Boolean))]}} : {})} as WorksWithRule;
  for (const m of rule.printers && 'models' in rule.printers ? rule.printers.models : []) if (!models.some((o) => o.id === m)) models.push({id: m, label: m, tip: m});
  const sizes = [...new Set([...NOZZLES, ...printers.map((p) => p.nozzle).filter((n) => n > 0), ...(rule.nozzle && 'sizes' in rule.nozzle ? rule.nozzle.sizes : [])])].sort((a, b) => a - b);
  const types = [...new Set([...filaments.map((f) => f.type).filter(Boolean), ...(rule.filaments && 'types' in rule.filaments ? rule.filaments.types : [])])].sort();
  const set = onChange && ((next: WorksWithRule) => onChange(next));
  const matched = stored ? printers.filter((p) => matchesPrinter(without(stored, 'exclude'), p)) : native;
  const working = stored ? matched.filter((p) => !stored.exclude?.includes(p.id)) : native;

  const printerMode = !rule.printers ? 'all' : 'models' in rule.printers ? 'models' : 'printers';
  const nozzleMode = !rule.nozzle ? 'any' : 'sizes' in rule.nozzle ? 'sizes' : 'range';
  const filamentMode = !rule.filaments ? 'any' : 'types' in rule.filaments ? 'types' : 'profiles';


  const slot = filaments.find((f) => f.id === slots[0] || f.name === slots[0]);
  const range = sizes.filter((n) => printers.some((p) => Math.abs(p.nozzle - n) < 0.0005));
  const list = (ids: string[], names: {id: string; label: string}[]) => ids.map((id) => names.find((n) => n.id === id)?.label ?? id).join(', ');
  const printerSummary = !rule.printers ? 'Any' : 'models' in rule.printers ? list(rule.printers.models, models) : list(rule.printers.printerIds, printers.map((p) => ({id: p.id, label: p.label})));
  const nozzleSummary = !rule.nozzle ? 'Any' : 'sizes' in rule.nozzle ? rule.nozzle.sizes.join(', ') : `${rule.nozzle.min}–${rule.nozzle.max}`;
  const filamentSummary = !rule.filaments ? 'Any' : 'types' in rule.filaments ? rule.filaments.types.join(', ') : list(rule.filaments.profileIds, filaments.map((f) => ({id: f.id, label: f.name})));
  const printSummary = !rule.prints ? 'Any' : list(rule.prints.profileIds, prints.map((p) => ({id: p.id, label: p.name})));
  const filamentCount = type === 'process' ? ` · ${plural(filaments.filter((f) => matchesFilament(rule, f)).length, 'filament')}` : ` · ${plural(prints.filter((p) => matchesPrint(rule, p.id)).length, 'print setting')}`;
  return <section className="works-with" aria-label="Works with">
    <div className="ww-rule">
      <Condition label="Printers" summary={printerSummary}>{set && <>
        <Choice label="Printers" value={printerMode} options={[{id: 'all', label: 'All'}, {id: 'models', label: 'Models'}, {id: 'printers', label: 'My printers'}]}
          onChange={(id) => set(id === 'all' ? without(rule, 'printers') : {...rule, printers: id === 'models' ? {models: [here?.printerModel || models[0]?.id].filter(Boolean)} : {printerIds: [mine.find((p) => p.id === here?.id)?.id ?? mine[0]?.id].filter((x): x is string => !!x)}})}/>
        <Modes active={printerMode} modes={{all: null,
          models: <Chips label="Printer models" options={models} value={printerMode === 'models' ? (rule.printers as {models: string[]}).models : []} onChange={(m) => set({...rule, printers: {models: m}})}/>,
          printers: <Chips label="My printers" options={mine.map((p) => ({id: p.id, label: p.label, group: p.brand, tip: [p.name, ...[p.printerModel, p.nozzle > 0 ? String(p.nozzle) : ''].filter((x) => x && !p.name.includes(x)).map((x) => x === String(p.nozzle) ? mm(p.nozzle) : x)].join(' · ')}))} value={printerMode === 'printers' ? (rule.printers as {printerIds: string[]}).printerIds : []} onChange={(ids) => set({...rule, printers: {printerIds: ids}})}/>}}/>
      </>}</Condition>
      <Condition label="Nozzle" summary={nozzleSummary}>{set && <>
        <Choice label="Nozzle" value={nozzleMode} options={[{id: 'any', label: 'Any'}, {id: 'sizes', label: 'Sizes'}, {id: 'range', label: 'Range'}]}
          onChange={(id) => set(id === 'any' ? without(rule, 'nozzle') : {...rule, nozzle: id === 'sizes' ? {sizes: [here?.nozzle || 0.4]} : {min: range[0] ?? 0.2, max: range[range.length - 1] ?? 0.8}})}/>
        <Modes active={nozzleMode} modes={{any: null,
          sizes: <Chips label="Nozzle sizes" options={sizes.map((n) => ({id: n, label: String(n)}))} value={nozzleMode === 'sizes' ? (rule.nozzle as {sizes: number[]}).sizes : []} onChange={(n) => set({...rule, nozzle: {sizes: n}})}/>,
          range: (() => {
          const r = nozzleMode === 'range' ? rule.nozzle as {min: number; max: number} : {min: range[0] ?? 0.2, max: range[range.length - 1] ?? 0.8}, put = (min: number, max: number) => {if (min > 0 && max >= min) set({...rule, nozzle: {min, max}});};
          return <span className="ww-range"><input className="text" type="number" inputMode="decimal" step={0.05} min={0.05} aria-label="Smallest nozzle" value={r.min} onChange={(e) => put(Number(e.target.value), r.max)}/>
            <span aria-hidden>–</span><input className="text" type="number" inputMode="decimal" step={0.05} min={0.05} aria-label="Largest nozzle" value={r.max} onChange={(e) => put(r.min, Number(e.target.value))}/><span className="ww-unit">mm</span></span>;
        })()}}/>
      </>}</Condition>
      {type === 'process' ? <Condition label="Filament" summary={filamentSummary}>{set && <>
        <Choice label="Filament" value={filamentMode} options={[{id: 'any', label: 'Any'}, {id: 'types', label: 'Types'}, {id: 'profiles', label: 'Filaments'}]}
          onChange={(id) => set(id === 'any' ? without(rule, 'filaments') : {...rule, filaments: id === 'types' ? {types: [slot?.type || types[0] || 'PLA']} : {profileIds: [slot?.id ?? filaments[0]?.id].filter((x): x is string => !!x)}})}/>
        <Modes active={filamentMode} modes={{any: null,
          types: <Chips label="Filament types" options={types.map((t) => ({id: t, label: t}))} value={filamentMode === 'types' ? (rule.filaments as {types: string[]}).types : []} onChange={(t) => set({...rule, filaments: {types: t}})}/>,
          profiles: <Chips label="Filaments" options={filaments.map((f) => ({id: f.id, label: f.name}))} value={filamentMode === 'profiles' ? (rule.filaments as {profileIds: string[]}).profileIds : []} onChange={(ids) => set({...rule, filaments: {profileIds: ids}})}/>}}/>
      </>}</Condition> : <Condition label="Print settings" summary={printSummary}>{set && <>
        <Choice label="Print settings" value={rule.prints ? 'profiles' : 'any'} options={[{id: 'any', label: 'Any'}, {id: 'profiles', label: 'Print settings'}]}
          onChange={(id) => set(id === 'any' ? without(rule, 'prints') : {...rule, prints: {profileIds: [prints.find((p) => p.id === process)?.id ?? prints[0]?.id].filter((x): x is string => !!x)}})}/>
        <Modes active={rule.prints ? 'profiles' : 'any'} modes={{any: null,
          profiles: <Chips label="Print settings" options={prints.map((p) => ({id: p.id, label: p.name}))} value={rule.prints?.profileIds ?? []} onChange={(ids) => set({...rule, prints: {profileIds: ids}})}/>}}/>
      </>}</Condition>}
    </div>
    <div className="ww-matched">
      <div className="ww-matched-head">{working.length ? `Works with ${plural(working.length, 'printer')}${filamentCount}` : <span className="works-with-none" role="alert">Works with no printer</span>}
        {stored && onChange && <> · Changed from base · <button type="button" className="link" onClick={() => onChange(undefined)}>Reset</button></>}</div>
      {matched.length > 0 && <ul className="ww-printers">{matched.map((p) => {
        const out = !!stored?.exclude?.includes(p.id);
        return <li key={p.id} className={out ? 'is-excluded' : undefined}>
          <span className="ww-printer-name">{p.label}</span><span className="ww-printer-detail">{p.detail}</span>
          {p.id === facts.current && <span className="works-with-used">In use</span>}
          {onChange && <button type="button" className="link ww-exclude" aria-label={`${out ? 'Include' : 'Exclude'} ${p.label}`} onClick={() => {
            const exclude = out ? rule.exclude!.filter((id) => id !== p.id) : [...rule.exclude ?? [], p.id];
            onChange(exclude.length ? {...rule, exclude} : without(rule, 'exclude'));
          }}>{out ? 'Include' : 'Exclude'}</button>}
        </li>;
      })}</ul>}
    </div>
  </section>;
}
