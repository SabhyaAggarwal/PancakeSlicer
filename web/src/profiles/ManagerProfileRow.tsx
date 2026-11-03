import { memo } from 'react';
import { Check } from '../components/Check';
import type { SelectionEvent } from './selection';

export interface ManagerProfileRowItem {
  key: string;
  name: string;
  note: string;
  slot?: number;
  cover?: string;
}

export const ManagerProfileRow = memo(function ManagerProfileRow({ item, listId, selected, focused, dispatch }: {
  item: ManagerProfileRowItem;
  listId: string;
  selected: boolean;
  focused: boolean;
  dispatch(event: SelectionEvent): void;
}) {
  return <div id={`${listId}:${item.key}`} data-profile-key={item.key} role="row" aria-selected={selected}
    className={`pf-item pf-selectable${selected ? ' is-on' : ''}`} tabIndex={focused ? 0 : -1}
    onMouseDown={event => { if (event.shiftKey) event.preventDefault(); }}
    onClick={event => dispatch({ type: 'CLICK', key: item.key, toggle: event.ctrlKey || event.metaKey, shift: event.shiftKey })}>
    <span role="gridcell" className="pf-row-check"><Check aria-label={`Select ${item.name}`} checked={selected}
      onClick={event => event.stopPropagation()} onCheckedChange={() => dispatch({ type: 'CLICK', key: item.key, toggle: true })}/></span>
    {item.cover && <span role="gridcell" className="pf-row-cover"><img src={item.cover} alt="" width={44} height={44} loading="lazy" decoding="async" onError={e => {e.currentTarget.hidden = true;}}/></span>}
    <span role="gridcell" className="pf-row-label"><span className="pf-item-title"><span className="pf-item-name">{item.name}</span>{item.slot !== undefined && <span className="pf-used">✓ In use</span>}</span>
      <span className="pf-dim pf-item-note">{item.note}</span></span>
  </div>;
});
