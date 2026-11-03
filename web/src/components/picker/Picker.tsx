import { useMemo, useRef, useState, type ComponentPropsWithRef, type ReactNode } from 'react';
import { Popover } from 'radix-ui';
import '../menu.css';
import './picker.css';
export interface PickerItem { id: string; label: string; sub?: string; swatch?: string | null; meta?: string; facet?: string; search?: string; disabled?: boolean | string }
export interface PickerGroup { id: string; title: string; items: PickerItem[]; lead?: boolean }
export interface PickerAction { label: string; run(): void; keep?: boolean }
export interface PickerCatalog { label: string; groups(): PickerGroup[]; onAdd(id: string): void }
interface Props { label: string; value?: string; groups(): PickerGroup[]; onPick(id: string): void; facetLabel?: string; memory?: string; footer?: PickerAction[]; catalog?: PickerCatalog; className?: string; triggerProps?: ComponentPropsWithRef<'button'> & { 'data-autofocus'?: boolean | string }; children: ReactNode }
const read = (key: string): string[] => { try { const v = JSON.parse(localStorage.getItem(key) ?? '[]'); return Array.isArray(v) ? v.filter((s) => typeof s === 'string') : []; } catch { return []; } };
const write = (key: string, value: string[]) => { try { localStorage.setItem(key, JSON.stringify(value)); } catch {  } };
export function Picker({ label, value, groups, onPick, memory = `orca.parity.${label}`, footer = [], catalog, className = '', triggerProps, children }: Props) {
  const [open, setOpen] = useState(false), [adding, setAdding] = useState(false), [query, setQuery] = useState(''), [rail, setRail] = useState('all'), [active, setActive] = useState(0);
  const [pins, setPins] = useState(() => read(`${memory}.pins`)), [recent, setRecent] = useState(() => read(`${memory}.recent`));
  const input = useRef<HTMLInputElement>(null), list = useRef<HTMLDivElement>(null);
  const source = useMemo(() => open ? (adding && catalog ? catalog.groups() : groups()).filter((g) => g.items.length) : [], [open, adding, groups, catalog]);
  const all = [...new Map(source.flatMap((g) => g.items).map((item) => [item.id, item])).values()];
  const rails = [{ id: 'all', title: adding ? 'All bases' : 'All profiles' }, ...(!adding && pins.some((id) => all.some((r) => r.id === id)) ? [{ id: 'pins', title: 'Favorites' }] : []), ...(!adding && recent.some((id) => all.some((r) => r.id === id)) ? [{ id: 'recent', title: 'Recent' }] : []), ...source.map(({ id, title }) => ({ id, title }))];
  const filtered = query ? all : rail === 'pins' ? all.filter((r) => pins.includes(r.id)) : rail === 'recent' ? recent.flatMap((id) => all.find((r) => r.id === id) ?? []) : source.find((g) => g.id === rail)?.items ?? all;
  const rows = filtered.filter((r) => `${r.label} ${r.search ?? ''} ${r.sub ?? ''}`.toLowerCase().includes(query.toLowerCase()));
  const enabled = rows.flatMap((r, i) => r.disabled ? [] : [i]);
  const pick = (row: PickerItem) => { if (row.disabled) return; if (adding && catalog) catalog.onAdd(row.id); else onPick(row.id); const next = [row.id, ...recent.filter((id) => id !== row.id)].slice(0, 8); setRecent(next); write(`${memory}.recent`, next); setOpen(false); };
  const pin = (row: PickerItem) => { const next = pins.includes(row.id) ? pins.filter((id) => id !== row.id) : [...pins, row.id]; setPins(next); write(`${memory}.pins`, next); };
  const move = (index: number) => { setActive(index); list.current?.querySelector(`[data-index="${index}"]`)?.scrollIntoView({ block: 'nearest' }); };
  return <Popover.Root open={open} onOpenChange={(next) => { setOpen(next); if (next) { setAdding(false); setQuery(''); setRail('all'); setActive(0); } }}><Popover.Trigger asChild><button {...triggerProps} type="button" className={`pk-trigger dropdown ${className}`} data-menu-trigger="orca" aria-label={label} title={triggerProps?.disabled ? `${label}\n${triggerProps.title ?? "Choose a compatible printer profile first."}` : triggerProps?.title ?? label}>{children}<span aria-hidden className="pk-caret">⌄</span></button></Popover.Trigger><Popover.Portal><Popover.Content className="orca-menu pk-pop" data-menu-family="orca" aria-label={adding ? catalog?.label : label} side="bottom" align="start" sideOffset={4} collisionPadding={8} onOpenAutoFocus={(e) => { e.preventDefault(); input.current?.focus(); }} onKeyDown={(e) => {
    if (e.key === 'Escape' || e.key === 'Tab') return;
    e.stopPropagation();
    const at = Math.max(0, enabled.indexOf(active));
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); move(enabled[(at + (e.key === 'ArrowDown' ? 1 : -1) + enabled.length) % enabled.length] ?? 0); }
    else if (e.key === 'Home' || e.key === 'End') { e.preventDefault(); move(enabled[e.key === 'Home' ? 0 : enabled.length - 1] ?? 0); }
    else if (e.key === 'Enter' && rows[active]) { e.preventDefault(); e.altKey ? pin(rows[active]) : pick(rows[active]); }
    else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && !query) { e.preventDefault(); const at = rails.findIndex((r) => r.id === rail); setRail(rails[(at + (e.key === 'ArrowRight' ? 1 : -1) + rails.length) % rails.length].id); setActive(0); }
  }}>
    <div className="pk-head">{adding && <button type="button" className="btn" onClick={() => { setAdding(false); setQuery(''); setRail('all'); }}>Back</button>}<input ref={input} className="text" aria-label={`Search ${adding ? 'catalog' : label.toLowerCase()}`} placeholder="Search…" value={query} onChange={(e) => { setQuery(e.target.value); setActive(0); }} /></div>
    <div className="pk-body"><nav className="pk-rail" aria-label={`${label} groups`}>{rails.map((r) => <button type="button" key={r.id} className={rail === r.id && !query ? 'active' : ''} onClick={() => { setRail(r.id); setQuery(''); setActive(0); input.current?.focus(); }}>{r.title}</button>)}</nav><div className="pk-list" role="listbox" aria-label={label} ref={list}>{rows.map((row, i) => <div key={row.id} className={`pk-row ${active === i ? 'is-active' : ''}`} data-index={i}><button type="button" role="option" aria-selected={value === row.id} aria-disabled={!!row.disabled} className="pk-option" title={row.disabled ? `${row.label}\n${typeof row.disabled === 'string' ? row.disabled : "Not available for the current printer."}` : row.label} onPointerMove={() => setActive(i)} onClick={() => pick(row)}><span className="pk-check" aria-hidden>{value === row.id ? '✓' : ''}</span><span className="pk-label">{row.label}{row.sub && <span className="pk-sub">{row.sub}</span>}</span>{row.meta && <span className="pk-meta">{row.meta}</span>}</button>{!adding && <button type="button" className="pk-star" title={pins.includes(row.id) ? 'Remove favorite' : 'Favorite'} aria-label={`Favorite ${row.label}`} aria-pressed={pins.includes(row.id)} onClick={() => pin(row)}>{pins.includes(row.id) ? '★' : '☆'}</button>}</div>)}{!rows.length && <div className="pk-empty">{query ? 'No matching profiles' : 'Add a profile from the catalog'}</div>}</div></div>
    <div className="pk-foot">{catalog && !adding && <button type="button" className="btn" onClick={() => { setAdding(true); setQuery(''); setRail('all'); setActive(0); input.current?.focus(); }}>{catalog.label}</button>}{footer.map((a) => <button type="button" className="btn" key={a.label} onClick={() => { if (!a.keep) setOpen(false); a.run(); }}>{a.label}</button>)}</div>
  </Popover.Content></Popover.Portal></Popover.Root>;
}
