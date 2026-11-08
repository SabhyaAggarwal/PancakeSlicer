export type ProfileKey = string;
interface SelectionContext {
  readonly scope: string;
  readonly order: readonly ProfileKey[];
  readonly anchor: ProfileKey | null;
  readonly focus: ProfileKey | null;
  readonly range: { readonly additive: boolean; readonly base: readonly ProfileKey[] } | null;
}
export type ProfileSelection = SelectionContext & (
  { readonly value: 'empty'; readonly selected: readonly [] }
  | { readonly value: 'selected'; readonly selected: readonly [ProfileKey, ...ProfileKey[]] }
);
export type SelectionEvent =
  | { type: 'SYNC'; scope: string; visible: readonly ProfileKey[]; remap?: ReadonlyMap<ProfileKey, ProfileKey> }
  | { type: 'CLICK'; key: ProfileKey; toggle?: boolean; shift?: boolean }
  | { type: 'SELECT_ALL' }
  | { type: 'SET'; keys: readonly ProfileKey[]; on: boolean }
  | { type: 'ARROW'; direction: 'previous' | 'next' | 'first' | 'last'; shift?: boolean; focusOnly?: boolean }
  | { type: 'ESCAPE' } | { type: 'CLEAR' };

export const selectionTransitions = {
  empty: { SYNC: 'derive', CLICK: 'derive', SELECT_ALL: 'derive', SET: 'derive', ARROW: 'derive', ESCAPE: 'empty', CLEAR: 'empty' },
  selected: { SYNC: 'derive', CLICK: 'derive', SELECT_ALL: 'derive', SET: 'derive', ARROW: 'derive', ESCAPE: 'empty', CLEAR: 'empty' },
} as const satisfies Record<ProfileSelection['value'], Record<SelectionEvent['type'], ProfileSelection['value'] | 'derive'>>;

const unique = (keys: readonly ProfileKey[]) => [...new Set(keys)];
function selection(context: SelectionContext, keys: readonly ProfileKey[]): ProfileSelection {
  const wanted = new Set(keys), selected = context.order.filter((key) => wanted.has(key));
  const [first, ...rest] = selected;
  return first === undefined ? { ...context, value: 'empty', selected: [] }
    : { ...context, value: 'selected', selected: [first, ...rest] };
}
export function createSelection(scope: string, visible: readonly ProfileKey[] = []): ProfileSelection {
  const order = unique(visible);
  return selection({ scope, order, anchor: null, focus: order[0] ?? null, range: null }, []);
}
export function selectedVisible(state: ProfileSelection): ProfileKey[] {
  const selected = new Set(state.selected);
  return unique(state.order).filter((key) => selected.has(key));
}
function pick(state: ProfileSelection, key: ProfileKey, shift = false, toggle = false): ProfileSelection {
  const index = state.order.indexOf(key);
  if (index < 0) return state;
  if (shift) {
    const anchor = state.anchor ?? state.focus ?? key;
    const from = state.order.indexOf(anchor), start = from < 0 ? index : from;
    const range = state.order.slice(Math.min(start, index), Math.max(start, index) + 1);
    const base = toggle ? state.range?.additive ? state.range.base : selectedVisible(state) : [];
    return selection({ ...state, anchor: state.order[start], focus: key, range: { additive: toggle, base } }, [...base, ...range]);
  }
  const current = selectedVisible(state);
  const selected = toggle ? current.includes(key) ? current.filter((id) => id !== key) : [...current, key] : [key];
  return selection({ ...state, anchor: key, focus: key, range: null }, selected);
}
export function reduceSelection(state: ProfileSelection, event: SelectionEvent): ProfileSelection {
  switch (event.type) {
    case 'SYNC': {
      const order = unique(event.visible);
      if (event.scope !== state.scope) return createSelection(event.scope, order);
      if (!event.remap?.size && order.length === state.order.length && order.every((key, i) => key === state.order[i])) return state;
      const remap = (key: ProfileKey) => event.remap?.get(key) ?? key;
      const visible = new Set(order), selected = state.selected.map(remap).filter((key) => visible.has(key));
      const oldFocus = state.focus === null ? null : remap(state.focus);
      const focus = oldFocus !== null && visible.has(oldFocus) ? oldFocus : selected[0] ?? order[0] ?? null;
      const oldAnchor = state.anchor === null ? null : remap(state.anchor);
      const anchor = oldAnchor !== null && visible.has(oldAnchor) ? oldAnchor : selected.includes(focus ?? '') ? focus : selected[0] ?? null;
      return selection({ scope: event.scope, order, focus, anchor, range: null }, selected);
    }
    case 'CLICK': return pick(state, event.key, event.shift, event.toggle);
    case 'SET': {
      const keys = new Set(event.keys), current = selectedVisible(state);
      return selection({ ...state, range: null, focus: event.keys[0] ?? state.focus }, event.on ? [...current, ...event.keys] : current.filter((key) => !keys.has(key)));
    }
    case 'SELECT_ALL': return selection({ ...state, anchor: state.order[0] ?? null, focus: state.focus ?? state.order[0] ?? null, range: null }, state.order);
    case 'ARROW': {
      if (!state.order.length) return state;
      const current = state.focus === null ? -1 : state.order.indexOf(state.focus);
      let index: number;
      switch (event.direction) {
        case 'first': index = 0; break;
        case 'last': index = state.order.length - 1; break;
        case 'previous': index = current < 0 ? state.order.length - 1 : Math.max(0, current - 1); break;
        case 'next': index = Math.min(state.order.length - 1, current + 1); break;
        default: { const never: never = event.direction; return never; }
      }
      const key = state.order[index];
      return event.focusOnly && !event.shift ? { ...state, focus: key, range: null } : pick(state, key, event.shift, event.focusOnly);
    }
    case 'ESCAPE': return selection({ ...state, anchor: null, range: null }, []);
    case 'CLEAR': return selection({ ...state, anchor: null, focus: null, range: null }, []);
    default: { const never: never = event; return never; }
  }
}
