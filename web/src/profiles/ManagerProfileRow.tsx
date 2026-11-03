import { memo, useState } from 'react';
import { Check } from '../components/Check';
import type { SelectionEvent } from './selection';

export interface ManagerProfileRowItem {
  key: string;
  name: string;
  note: string;
  meta?: string;
  fullName?: string;
  slot?: number;
  cover?: string | null;
  swatch?:string|null;
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
  return <div id={`${listId}:${item.key}`} data-profile-key={item.key} role="row" aria-label={item.fullName} aria-selected={selected}
    className={`pf-item pf-selectable${selected ? ' is-on' : ''}`} tabIndex={focused ? 0 : -1}
    onMouseDown={event => { if (event.shiftKey) event.preventDefault(); }}
    onClick={event => dispatch({ type: 'CLICK', key: item.key, toggle: selectable && (event.ctrlKey || event.metaKey), shift: selectable && event.shiftKey })}>
    {selectable && <span role="gridcell" className="pf-row-check"><Check aria-label={`Select ${item.fullName ?? item.name}`} checked={selected}
      onClick={event => event.stopPropagation()} onCheckedChange={() => dispatch({ type: 'CLICK', key: item.key, toggle: true })}/></span>}
    {item.swatch !== undefined && (item.swatch ? <span role="gridcell" className="profile-color-swatch" style={{backgroundColor:item.swatch}}/> : <span role="gridcell" className="pf-row-swatch" aria-label="No color"/>)}
    {item.cover !== undefined && <span role="gridcell" className="pf-row-cover">{item.cover && failedCover !== item.cover && <img src={item.cover} alt="" width={44} height={44} loading="lazy" decoding="async" onError={()=>setFailedCover(item.cover ?? undefined)}/>}</span>}
    <span role="gridcell" className="pf-row-label"><span className="pf-item-title"><span className="pf-item-name">{item.name}</span>{item.meta && <span className="pf-item-meta">{item.meta}</span>}{item.slot !== undefined && <span className="pf-used">✓ In use</span>}</span>
      {item.note && <span className="pf-dim pf-item-note">{item.note}</span>}</span>
  </div>;
});
