


const BOUND = '[data-menu-bound], .topbar, .tabbar, .sidebar, .pf-window, .modal, .optgroup, .opt-line';
export type Edge = { align: 'start' | 'end'; alignOffset: number };


export function edgeOf(trigger: Element | null | undefined, textInset = 0): Edge {
  if (!trigger) return { align: 'start', alignOffset: 0 };
  const r = trigger.getBoundingClientRect(), box = (trigger.closest(BOUND) ?? document.documentElement).getBoundingClientRect();


  if (r.left > box.left + box.width / 2) return { align: 'end', alignOffset: 0 };
  return { align: 'start', alignOffset: textInset ? -(textInset - (parseFloat(getComputedStyle(trigger).paddingLeft) || 0)) : 0 };
}

export const MENU_TEXT_INSET = 34;
