import { preferenceJson, savePreferences, usePreference } from '../../saved-preferences';
import { countRender } from '../../interaction-metrics';
import { Fragment, memo, useCallback, useId, useMemo, useRef, useState, type ComponentPropsWithRef, type ReactNode } from 'react';
import { Popover } from 'radix-ui';
import { useRowFill } from '../tab-rows';
import '../menu.css';
import './picker.css';
import { Caret } from '../Caret';
import { Glyph } from '../Glyph';
export interface PickerItem { id: string; label: string; sub?: string; swatch?: string | null; meta?: string; facet?: string; search?: string; disabled?: boolean | string;
  image?: string | null; status?: { label: string; tone: 'online' | 'printing' | 'offline' | 'error' } }
export interface PickerGroup { id: string; title: string; items: PickerItem[]; lead?: boolean }
export interface PickerAction { label: string; run(): void; keep?: boolean }
export interface PickerCatalog { label: string; groups(): PickerGroup[]; onAdd(id: string): void }
export const selectDefaultRail = (source: readonly PickerGroup[], pins: readonly string[]) => source.some(g => g.items.some(item => !item.disabled && pins.includes(item.id))) ? 'pins' : source.find(g => g.items.some(item => !item.disabled))?.id ?? 'all';
interface Props { label: string; value?: string; groups(): PickerGroup[]; sections?: boolean; onPick(id: string): void; facetLabel?: string; memory?: string; footer?: PickerAction[]; catalog?: PickerCatalog; className?: string; triggerProps?: ComponentPropsWithRef<'button'> & { 'data-autofocus'?: boolean | string }; children: ReactNode }
const read = (key: string): string[] => { const v = preferenceJson<unknown>(key, []); return Array.isArray(v) ? v.filter((s): s is string => typeof s === 'string') : []; };
const write = (key: string, value: string[]) => savePreferences({[key]:value});
const hasOwn = (groups: () => PickerGroup[]) => groups().some(g => g.items.some(item => !item.disabled));
const splitTail = (label: string): [string, string] => {
  for (const re of [/\s\S+\s\S+$/, /\s\S+$/]) { const m = re.exec(label); if (m && m[0].length <= 15 && m.index >= 4) return [label.slice(0, m.index), m[0]]; }
  return [label, ''];
};
function PickerArt({ src }: { src: string | null }) {
  const [failed, setFailed] = useState<string>();
  return <span className="pk-art" aria-hidden>{src && failed !== src ? <img src={src} alt="" loading="lazy" draggable={false} onError={() => setFailed(src)}/> : <Glyph name="printer" size={40}/>}</span>;
}
const PickerRow = memo(function PickerRow({ row, index, id, active, selected, adding, pinned, onActive, onPick, onPin }: {
  row: PickerItem; index: number; id: string; active: boolean; selected: boolean; adding: boolean; pinned: boolean;
  onActive(index: number): void; onPick(row: PickerItem): void; onPin(row: PickerItem): void;
}) {
  countRender('picker-row');
  const media = row.image !== undefined, [head, tail] = splitTail(row.label);
  return <div className={`pk-row ${active ? 'is-active' : ''}`} data-index={index}>
    <button id={`${id}-${index}`} type="button" role="option" tabIndex={active ? 0 : -1} aria-selected={selected} aria-disabled={!!row.disabled} className="pk-option" onFocus={() => onActive(index)} onPointerMove={() => onActive(index)} data-full-text={[row.label, row.sub, row.meta].filter(Boolean).join(' · ')} onClick={() => onPick(row)}>
      <span className="pk-check" aria-hidden>{selected ? '✓' : ''}</span>{media && <PickerArt src={row.image ?? null}/>}{row.swatch !== undefined && <span className="profile-color-swatch" data-unknown={!row.swatch} style={{backgroundColor:row.swatch ?? undefined}}/>}
      <span className="pk-text"><span className="pk-label"><span className="pk-head">{head}</span>{tail && <span className="pk-tail">{tail}</span>}</span>{row.sub && <span className="pk-sub">{row.sub}</span>}{media && row.meta && <span className="pk-sub">{row.meta}</span>}{row.status && <span className="pk-status" data-tone={row.status.tone}>{row.status.label}</span>}</span>
      {!media && row.meta && <span className="pk-meta">{row.meta}</span>}
    </button>
    {!adding && <button type="button" className="pk-star" aria-label={`Favorite ${row.label}`} aria-pressed={pinned} onClick={() => onPin(row)}>{pinned ? '★' : '☆'}</button>}
  </div>;
});
export function Picker({ label, value, groups, sections = false, onPick, memory = `orca.parity.${label}`, footer = [], catalog, className = '', triggerProps, children }: Props) {
  countRender('picker');
  const [open, setOpen] = useState(false), [adding, setAdding] = useState(false), [rail, setRail] = useState(''), [active, setActive] = useState(0);
  const savedPins = usePreference(`${memory}.pins`);
  const pins = useMemo(() => read(`${memory}.pins`), [memory,savedPins]);
  const railRef = useRef<HTMLElement>(null), list = useRef<HTMLDivElement>(null), typed = useRef({text: '', at: 0});
  const id = useId();
  const source = useMemo(() => open ? (adding && catalog ? catalog.groups() : groups()).map(g => ({...g, items:g.items.filter(item => !item.disabled)})).filter((g) => g.items.length) : [], [open, adding, groups, catalog]);
  const ownRows = useMemo(() => open && adding && hasOwn(groups), [open, adding, groups]);
  const all = useMemo(() => [...new Map(source.flatMap((g) => g.items).map((item) => [item.id, item])).values()], [source]);
  const pinnedSet = useMemo(() => new Set(pins), [pins]);
  const sectioned = useMemo(() => sections ? [{title: 'Favorites', items: all.filter(r => pinnedSet.has(r.id))}, ...source.map(g => ({title: g.title, items: g.items.filter(r => !pinnedSet.has(r.id))}))].filter(g => g.items.length) : null, [sections, all, pinnedSet, source]);
  const rails = useMemo(() => sections ? [] : [...(all.some(r => pinnedSet.has(r.id)) ? [{id:'pins',title:'Favorites'}] : []), {id:'all',title:adding ? 'All bases' : 'All profiles'}, ...source.map(({id,title}) => ({id,title}))], [sections, adding, pinnedSet, all, source]);
  useRowFill(railRef, open && rails.map(r => r.id).join('|'));
  const activeRail = rails.some(r => r.id === rail) ? rail : selectDefaultRail(source, pins);
  const rows = useMemo(() => sectioned ? sectioned.flatMap(g => g.items) : activeRail === 'pins' ? all.filter(r => pinnedSet.has(r.id)) : activeRail === 'all' ? all : source.find(g => g.id === activeRail)?.items ?? [], [sectioned, activeRail, all, pinnedSet, source]);
  const heads = useMemo(() => { const at = new Map<number, string>(); let i = 0; for (const g of sectioned ?? []) { at.set(i, g.title); i += g.items.length; } return at; }, [sectioned]);
  const enabled = useMemo(() => rows.flatMap((r, i) => r.disabled ? [] : [i]), [rows]);
  const resetType = () => { typed.current = {text: '', at: 0}; };
  const move = (index: number) => {
    if (index < 0 || !rows[index] || rows[index].disabled) return;
    setActive(index);
    const el = list.current?.querySelector<HTMLButtonElement>(`[data-index="${index}"] .pk-option`);
    el?.focus({preventScroll: true}); el?.scrollIntoView({ block: 'nearest' });
  };
  const changeRail = (next: string) => { setRail(next); setActive(0); resetType(); };
  const changeCatalog = (next: boolean) => { setAdding(next); changeRail(''); };
  const pick = useCallback((row: PickerItem) => {
    if (row.disabled) return;
    if (adding && catalog) catalog.onAdd(row.id); else onPick(row.id);
    setOpen(false); resetType();
  }, [adding, catalog, onPick]);
  const pin = useCallback((row: PickerItem) => { const next = pins.includes(row.id) ? pins.filter((id) => id !== row.id) : [...pins, row.id]; write(`${memory}.pins`, next); }, [pins, memory]);
  return <Popover.Root open={open} onOpenChange={next => { setOpen(next); resetType(); if (next) { setAdding(!!catalog && !hasOwn(groups)); setRail(''); setActive(0); } }}>
    <Popover.Trigger asChild><button {...triggerProps} type="button" className={`pk-trigger dropdown ${className}`} data-menu-trigger="orca" aria-label={label} onKeyDown={e => {
      triggerProps?.onKeyDown?.(e);
      if (!e.defaultPrevented && (e.key === 'ArrowDown' || e.key === 'ArrowUp')) { e.preventDefault(); e.stopPropagation(); resetType(); setAdding(!!catalog && !hasOwn(groups)); setRail(''); setOpen(true); }
    }}>{children}<Caret className="pk-caret"/></button></Popover.Trigger>
    <Popover.Portal><Popover.Content className={`orca-menu pk-pop ${rows.some(r => r.image !== undefined) ? 'pk-media' : ''}`} data-menu-family="orca" aria-label={adding ? catalog?.label : label} side="bottom" align="start" sideOffset={4} collisionPadding={8} onOpenAutoFocus={e => { e.preventDefault(); const selected = rows.findIndex(r => r.id === value && !r.disabled); move(selected >= 0 ? selected : enabled[0] ?? -1); }} onKeyDown={e => {
      if (e.key === 'Escape' || e.key === 'Tab') return;
      e.stopPropagation();
      const at = enabled.indexOf(active);
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); resetType(); if (enabled.length) move(enabled[(at + (e.key === 'ArrowDown' ? 1 : -1) + enabled.length) % enabled.length]); }
      else if (e.key === 'Home' || e.key === 'End') { e.preventDefault(); resetType(); move(enabled[e.key === 'Home' ? 0 : enabled.length - 1] ?? -1); }
      else if (e.key === 'Enter' && e.target instanceof Element && e.target.closest('.pk-option') && rows[active]) { e.preventDefault(); e.altKey ? pin(rows[active]) : pick(rows[active]); }
      else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && rails.length) { e.preventDefault(); const at = rails.findIndex(r => r.id === activeRail); changeRail(rails[(at + (e.key === 'ArrowRight' ? 1 : -1) + rails.length) % rails.length].id); }
      else if (e.key.length === 1 && e.key !== ' ' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault();
        const now = Date.now(), old = now - typed.current.at < 700 ? typed.current.text : '';
        const key = e.key.toLocaleLowerCase(), term = old === key ? key : old + key;
        typed.current = {text: term, at: now};
        const ordered = [...enabled.filter(i => i > active), ...enabled.filter(i => i <= active)];
        const match = ordered.find(i => `${rows[i].label} ${rows[i].sub ?? ''} ${rows[i].search ?? ''}`.toLocaleLowerCase().split(/\s+/).some(word => word.startsWith(term)) || rows[i].label.toLocaleLowerCase().replace(/^my\s+/, '').startsWith(term));
        if (match !== undefined) move(match);
      }
    }}>
      <div className={`pk-body ${rails.length > 1 ? '' : 'no-rail'}`}>{rails.length > 1 && <nav ref={railRef} className="pk-rail" aria-label={`${label} groups`}>{rails.map(r => <button type="button" key={r.id} className={activeRail === r.id ? 'active' : ''} aria-pressed={activeRail === r.id} onClick={() => changeRail(r.id)}>{r.title}</button>)}</nav>}
        <div className="pk-list" role="listbox" aria-label={label} ref={list}>{rows.map((row, i) => <Fragment key={`${i}:${row.id}`}>{heads.has(i) && <div className="pk-group" role="presentation">{heads.get(i)}</div>}<PickerRow row={row} index={i} id={id} active={active === i} selected={value === row.id} adding={adding} pinned={pinnedSet.has(row.id)} onActive={setActive} onPick={pick} onPin={pin} /></Fragment>)}{!rows.length && <div className="pk-empty">{catalog && !adding ? 'None yet' : 'No profiles'}</div>}</div>
      </div>
      <div className="pk-foot">{adding && ownRows && <button type="button" className="btn-ghost" onClick={() => changeCatalog(false)}>Back</button>}{catalog && !adding && <button type="button" className="btn" onClick={() => changeCatalog(true)}>{catalog.label}</button>}{footer.map(a => <button type="button" className="btn" key={a.label} onClick={() => { if (!a.keep) setOpen(false); a.run(); }}>{a.label}</button>)}</div>
    </Popover.Content></Popover.Portal>
  </Popover.Root>;
}
