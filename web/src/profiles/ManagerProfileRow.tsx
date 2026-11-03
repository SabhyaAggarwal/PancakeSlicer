import { memo, useState } from 'react';
import { Check } from '../components/Check';
import { Glyph } from '../components/Glyph';
import type { SelectionEvent } from './selection';

export interface ManagerProfileRowItem {
  key: string;
  name: string;
  note: string;
  meta?: string;
  fullName?: string;
  child?: boolean;
  slot?: number;
  cover?: string | null;
  swatch?:string|null;
}

function splitName(name: string): [string, string] {
  for (const re of [/\s\S+\s\S+$/, /\s\S+$/]) { const m = re.exec(name); if (m && m[0].length <= 15 && m.index >= 4) return [name.slice(0, m.index), '\u00a0' + m[0].slice(1)]; }
  return ['', name];
}

export const ManagerProfileRow = memo(function ManagerProfileRow({ item, listId, selected, focused, selectable = true, dispatch }: {
  item: ManagerProfileRowItem;
  listId: string;
  selected: boolean;
  focused: boolean;
  selectable?: boolean;
  dispatch(event: SelectionEvent): void;
}) {
  const [failedCover,setFailedCover]=useState<string>();
  const [head,tail]=splitName(item.name);
  return <div id={`${listId}:${item.key}`} data-profile-key={item.key} role="row" aria-label={item.fullName} aria-selected={selected} data-child={item.child || undefined}
    className={`pf-item pf-selectable${selected ? ' is-on' : ''}`} tabIndex={focused ? 0 : -1}
    onMouseDown={event => { if (event.shiftKey) event.preventDefault(); }}
    onClick={event => dispatch({ type: 'CLICK', key: item.key, toggle: selectable && (event.ctrlKey || event.metaKey), shift: selectable && event.shiftKey })}>
    {selectable && <span role="gridcell" className="pf-row-check"><Check aria-label={`Select ${item.fullName ?? item.name}`} checked={selected}
      onClick={event => event.stopPropagation()} onCheckedChange={() => dispatch({ type: 'CLICK', key: item.key, toggle: true })}/></span>}
    {item.swatch !== undefined && (item.swatch ? <span role="gridcell" className="profile-color-swatch" style={{backgroundColor:item.swatch}}/> : <span role="gridcell" className="pf-row-swatch" aria-label="No color"/>)}
    {item.cover !== undefined && <span role="gridcell" className="pf-row-cover">{item.cover && failedCover !== item.cover ? <img src={item.cover} alt="" width={52} height={52} loading="lazy" decoding="async" onError={()=>setFailedCover(item.cover ?? undefined)}/> : <Glyph name="printer" size={28}/>}</span>}
    <span role="gridcell" className="pf-row-label"><span className="pf-item-name">{head && <span className="pf-name-head">{head}</span>}<span className="pf-name-tail">{tail}</span></span>
      {item.meta && <span className="pf-item-meta">{item.meta}</span>}{item.note && <span className="pf-item-note">{item.note}</span>}</span>
    {item.slot !== undefined && <span role="gridcell" className="pf-row-used" aria-label="In use"><svg viewBox="0 0 12 12" width="12" height="12" aria-hidden><path d="M2.5 6.5 5 9l4.5-6"/></svg></span>}
  </div>;
});
