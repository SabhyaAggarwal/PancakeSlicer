import { useMemo } from 'react';
import type { Config, PresetType, UserPreset } from '../api/types';
import { Choice } from '../components/kit';
import { getState, useStore } from '../state/store';
import { selectNativeCompatibility } from './compatibility';
import { profileKey } from './identity';
import { modelLabel } from './metadata';
import { findCatalogBase, resolveProfile } from './model';
import { fromApplicability1, matchesFilament, matchesNozzle, matchesPrint, matchesPrinter, type PrinterFacts, type WorksWithRule } from './works-with';

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

type Printer = PrinterFacts & {id: string; name: string; model: string; label: string; detail: string};
function libraryFacts() {
  const s = getState(), live = s.userPresets.filter((p) => !p.archived);
  const printers: Printer[] = live.filter((p) => p.type === 'machine').map((p) => {
    const id = profileKey(p), c = resolveProfile(s, 'machine', id).config, vendor = findCatalogBase(s, 'machine', p.base ?? p.inherits ?? '').base?.vendor ?? '';
    const model = modelLabel(c, vendor), nozzle = Number(first(c.nozzle_diameter));
    return {id, name: p.name, printerModel: first(c.printer_model), model, nozzle, label: p.kind === 'master' ? model : p.name, detail: [p.kind === 'master' ? '' : model, nozzle > 0 ? mm(nozzle) : ''].filter(Boolean).join(' · ')};
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
function Chips<T extends string | number>({label, options, value, onChange}: {label: string; options: {id: T; label: string}[]; value: T[]; onChange(next: T[]): void}) {
  return <div className="chips ww-chips" role="group" aria-label={label}>{options.map((o) => {
    const on = value.includes(o.id), last = on && value.length === 1;
    return <button key={String(o.id)} type="button" className="chip" aria-pressed={on} aria-disabled={last || undefined}
      onClick={() => {if (!last) onChange(on ? value.filter((v) => v !== o.id) : [...value, o.id]);}}>{o.label}</button>;
  })}</div>;
}
function Condition({label, count, children}: {label: string; count: string; children: React.ReactNode}) {
  return <div className="ww-condition" role="group" aria-label={label}><span className="ww-label">{label}</span><div className="ww-choice">{children}</div><span className="ww-count">{count}</span></div>;
}

export function WorksWith({source, config, value, onChange}: {source: UserPreset; config: Config; value?: unknown; onChange(value: WorksWithRule | undefined): void}) {
  const users = useStore((s) => s.userPresets), slots = useStore((s) => s.filamentSlots), process = useStore((s) => s.process);
  const facts = useMemo(libraryFacts, [users]);
  const rule = worksWithRule(value), type = source.type, key = profileKey(source);
  const native = useMemo(() => {
    if (rule) return [];
    const s = getState(), draft = {...source, overlay: config, overrides: config}, state = {...s, userPresets: s.userPresets.map((p) => profileKey(p) === key ? draft : p)};
    return facts.printers.filter((p) => selectNativeCompatibility(state, type, key, p.id).ok);
  }, [rule, facts, config, source, type, key]);
  if (type === 'machine') return null;
  const {printers, filaments, prints} = facts, here = printers.find((p) => p.id === facts.current) ?? printers[0];
  const models = [...new Map(printers.filter((p) => p.printerModel).map((p) => [p.printerModel, p.model || p.printerModel])).entries()].map(([id, label]) => ({id, label}));
  for (const m of rule?.printers && 'models' in rule.printers ? rule.printers.models : []) if (!models.some((o) => o.id === m)) models.push({id: m, label: m});
  const sizes = [...new Set([...NOZZLES, ...printers.map((p) => p.nozzle).filter((n) => n > 0), ...(rule?.nozzle && 'sizes' in rule.nozzle ? rule.nozzle.sizes : [])])].sort((a, b) => a - b);
  const types = [...new Set([...filaments.map((f) => f.type).filter(Boolean), ...(rule?.filaments && 'types' in rule.filaments ? rule.filaments.types : [])])].sort();
  const set = (next: WorksWithRule) => onChange(next);
  const base = rule ?? {schema: 2} as WorksWithRule;
  const matched = rule ? printers.filter((p) => matchesPrinter(without(rule, 'exclude'), p)) : native;
  const working = rule ? matched.filter((p) => !rule.exclude?.includes(p.id)) : native;

  const printerMode = !rule?.printers ? 'all' : 'models' in rule.printers ? 'models' : 'printers';
  const nozzleMode = !rule?.nozzle ? 'any' : 'sizes' in rule.nozzle ? 'sizes' : 'range';
  const filamentMode = !rule?.filaments ? 'any' : 'types' in rule.filaments ? 'types' : 'profiles';
  const slot = filaments.find((f) => f.id === slots[0] || f.name === slots[0]);
  const range = sizes.filter((n) => printers.some((p) => Math.abs(p.nozzle - n) < 0.0005));
  return <section className="works-with" aria-label="Works with">
    <Choice label="Works with" value={rule ? 'rule' : 'base'} options={[{id: 'base', label: 'Same as base'}, {id: 'rule', label: 'Custom'}]}
      onChange={(id) => onChange(id === 'base' ? undefined : {schema: 2, printers: {printerIds: native.length ? native.map((p) => p.id) : here ? [here.id] : []}})}/>
    {rule && <div className="ww-rule">
      <Condition label="Printers" count={plural(printers.filter((p) => matchesPrinter({schema: 2, printers: rule.printers}, p)).length, 'printer')}>
        <Choice label="Printers" value={printerMode} options={[{id: 'all', label: 'All'}, {id: 'models', label: 'Models'}, {id: 'printers', label: 'My printers'}]}
          onChange={(id) => set(id === 'all' ? without(base, 'printers') : {...base, printers: id === 'models' ? {models: [here?.printerModel || models[0]?.id].filter(Boolean)} : {printerIds: here ? [here.id] : []}})}/>
        {printerMode === 'models' && <Chips label="Printer models" options={models} value={(rule.printers as {models: string[]}).models} onChange={(m) => set({...base, printers: {models: m}})}/>}
        {printerMode === 'printers' && <Chips label="My printers" options={printers.map((p) => ({id: p.id, label: p.label}))} value={(rule.printers as {printerIds: string[]}).printerIds} onChange={(ids) => set({...base, printers: {printerIds: ids}})}/>}
      </Condition>
      <Condition label="Nozzle" count={plural(printers.filter((p) => matchesNozzle(rule, p.nozzle)).length, 'printer')}>
        <Choice label="Nozzle" value={nozzleMode} options={[{id: 'any', label: 'Any'}, {id: 'sizes', label: 'Sizes'}, {id: 'range', label: 'Range'}]}
          onChange={(id) => set(id === 'any' ? without(base, 'nozzle') : {...base, nozzle: id === 'sizes' ? {sizes: [here?.nozzle || 0.4]} : {min: range[0] ?? 0.2, max: range[range.length - 1] ?? 0.8}})}/>
        {nozzleMode === 'sizes' && <Chips label="Nozzle sizes" options={sizes.map((n) => ({id: n, label: String(n)}))} value={(rule.nozzle as {sizes: number[]}).sizes} onChange={(n) => set({...base, nozzle: {sizes: n}})}/>}
        {nozzleMode === 'range' && (() => {
          const r = rule.nozzle as {min: number; max: number}, put = (min: number, max: number) => {if (min > 0 && max >= min) set({...base, nozzle: {min, max}});};
          return <span className="ww-range"><input className="text" type="number" inputMode="decimal" step={0.05} min={0.05} aria-label="Smallest nozzle" value={r.min} onChange={(e) => put(Number(e.target.value), r.max)}/>
            <span aria-hidden>–</span><input className="text" type="number" inputMode="decimal" step={0.05} min={0.05} aria-label="Largest nozzle" value={r.max} onChange={(e) => put(r.min, Number(e.target.value))}/><span className="ww-unit">mm</span></span>;
        })()}
      </Condition>
      {type === 'process' ? <Condition label="Filament" count={plural(filaments.filter((f) => matchesFilament(rule, f)).length, 'filament')}>
        <Choice label="Filament" value={filamentMode} options={[{id: 'any', label: 'Any'}, {id: 'types', label: 'Types'}, {id: 'profiles', label: 'Filaments'}]}
          onChange={(id) => set(id === 'any' ? without(base, 'filaments') : {...base, filaments: id === 'types' ? {types: [slot?.type || types[0] || 'PLA']} : {profileIds: [slot?.id ?? filaments[0]?.id].filter((x): x is string => !!x)}})}/>
        {filamentMode === 'types' && <Chips label="Filament types" options={types.map((t) => ({id: t, label: t}))} value={(rule.filaments as {types: string[]}).types} onChange={(t) => set({...base, filaments: {types: t}})}/>}
        {filamentMode === 'profiles' && <Chips label="Filaments" options={filaments.map((f) => ({id: f.id, label: f.name}))} value={(rule.filaments as {profileIds: string[]}).profileIds} onChange={(ids) => set({...base, filaments: {profileIds: ids}})}/>}
      </Condition> : <Condition label="Print settings" count={plural(prints.filter((p) => matchesPrint(rule, p.id)).length, 'print setting')}>
        <Choice label="Print settings" value={rule.prints ? 'profiles' : 'any'} options={[{id: 'any', label: 'Any'}, {id: 'profiles', label: 'Print settings'}]}
          onChange={(id) => set(id === 'any' ? without(base, 'prints') : {...base, prints: {profileIds: [prints.find((p) => p.id === process)?.id ?? prints[0]?.id].filter((x): x is string => !!x)}})}/>
        {rule.prints && <Chips label="Print settings" options={prints.map((p) => ({id: p.id, label: p.name}))} value={rule.prints.profileIds} onChange={(ids) => set({...base, prints: {profileIds: ids}})}/>}
      </Condition>}
    </div>}
    <div className="ww-matched">
      <div className="ww-matched-head">{working.length ? `Works with ${plural(working.length, 'printer')}` : <span className="works-with-none" role="alert">Works with no printer</span>}</div>
      {matched.length > 0 && <ul className="ww-printers">{matched.map((p) => {
        const out = !!rule?.exclude?.includes(p.id);
        return <li key={p.id} className={out ? 'is-excluded' : undefined}>
          <span className="ww-printer-name">{p.label}</span><span className="ww-printer-detail">{p.detail}</span>
          {p.id === facts.current && <span className="works-with-used">In use</span>}
          {rule && <button type="button" className="btn-ghost ww-exclude" aria-label={`${out ? 'Include' : 'Exclude'} ${p.label}`} onClick={() => {
            const exclude = out ? rule.exclude!.filter((id) => id !== p.id) : [...rule.exclude ?? [], p.id];
            set(exclude.length ? {...rule, exclude} : without(rule, 'exclude'));
          }}>{out ? 'Include' : 'Exclude'}</button>}
        </li>;
      })}</ul>}
    </div>
  </section>;
}
