const BOUND = '[data-menu-bound], .topbar, .tabbar, .sidebar, .pf-window, .modal, .optgroup, .opt-line';
export type Edge = { align: 'start' | 'end' };
export function edgeOf(trigger: Element | null | undefined): Edge {
  if (!trigger) return { align: 'start' };
  const r = trigger.getBoundingClientRect(), box = (trigger.closest(BOUND) ?? document.documentElement).getBoundingClientRect();
  return { align: r.left > box.left + box.width / 2 ? 'end' : 'start' };
}
