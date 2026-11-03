import { disabledCommandReason } from './control-reason';
import { forwardRef, useRef, useState, type ButtonHTMLAttributes, type HTMLAttributes, type ReactNode } from 'react';
import { edgeOf, MENU_TEXT_INSET, type Edge } from './anchor';
import { DropdownMenu as Menu } from 'radix-ui';
import './menu.css';

export const MenuSurface = forwardRef<HTMLDivElement, HTMLAttributes<HTMLDivElement>>(function MenuSurface({ className = '', ...props }, ref) {
  return <div {...props} ref={ref} data-menu-family="orca" className={`orca-menu ${className}`} />;
});
export function MenuGroup({ children }: { children: ReactNode; level?: 1 | 2 }) { return <Menu.Label className="orca-menu-group">{children}</Menu.Label>; }
export function MenuSeparator() { return <Menu.Separator className="menu-sep" />; }
export type MenuEntry = 'sep' | { label: string; shortcut?: string; action?(): void; disabled?: boolean; checked?: boolean; role?: 'menuitem' | 'menuitemcheckbox' | 'menuitemradio'; tip?: string; sub?: MenuEntry[]; header?: boolean; level?: 1 | 2; keepOpen?: boolean; icon?: ReactNode };
function MenuRows({ items }: { items: MenuEntry[] }) {
  return items.map((it, i) => {
    if (it === 'sep') return <MenuSeparator key={i} />;
    if (it.header) return <MenuGroup key={i}>{it.label}</MenuGroup>;
    const content = <><span className="menu-check" aria-hidden>{it.checked ? '✓' : ''}</span><span className="orca-menu-label">{it.icon}{it.label}</span><span className="orca-menu-key">{it.sub ? '›' : it.shortcut}</span></>;
    if (it.sub) return <Menu.Sub key={i}><Menu.SubTrigger className="menu-item" disabled={it.disabled} aria-description={it.disabled || !it.action && !it.sub ? it.tip ?? disabledCommandReason(it.label) : it.tip}>{content}</Menu.SubTrigger><Menu.Portal><Menu.SubContent className="orca-menu" data-menu-family="orca" collisionPadding={8} sideOffset={2}><MenuRows items={it.sub} /></Menu.SubContent></Menu.Portal></Menu.Sub>;
    return <Menu.Item key={i} className="menu-item" disabled={it.disabled || !it.action} aria-description={it.disabled || !it.action && !it.sub ? it.tip ?? disabledCommandReason(it.label) : it.tip} onSelect={(e) => { if (it.keepOpen) e.preventDefault(); it.action?.(); }}>{content}</Menu.Item>;
  });
}
export const MenuButton = forwardRef<HTMLButtonElement, Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'children'> & { label: string; items: MenuEntry[]; children?: ReactNode }>(function MenuButton({ label, items, children, ...props }, ref) {
  const own = useRef<HTMLButtonElement | null>(null), [edge, setEdge] = useState<Edge>({ align: 'start', alignOffset: 0 });
  const setRef = (el: HTMLButtonElement | null) => { own.current = el; if (typeof ref === 'function') ref(el); else if (ref) ref.current = el; };
  return <Menu.Root onOpenChange={(open) => { if (open) setEdge(edgeOf(own.current, MENU_TEXT_INSET)); }}><Menu.Trigger asChild><button {...props} type="button" ref={setRef} data-menu-trigger="orca" aria-label={props['aria-label'] ?? label}>{children ?? label}</button></Menu.Trigger><Menu.Portal><Menu.Content className="orca-menu" data-menu-family="orca" aria-label={label} side="bottom" align={edge.align} alignOffset={edge.alignOffset} sideOffset={4} collisionPadding={8}><MenuRows items={items} /></Menu.Content></Menu.Portal></Menu.Root>;
});
export function ActionMenu({ items, label, at, anchor, onClose, returnFocus }: { items: MenuEntry[]; label: string; at?: { x: number; y: number }; anchor?: HTMLElement | null; onClose(): void; returnFocus?: HTMLElement | null }) {
  const [open, setOpen] = useState(true);
  const r = anchor?.getBoundingClientRect();
  return <Menu.Root open={open} onOpenChange={(next) => { setOpen(next); if (!next) onClose(); }}><Menu.Trigger className="menu-position-anchor" aria-label={label} style={{ position: 'fixed', left: r?.left ?? at?.x ?? 8, top: r?.bottom ?? at?.y ?? 8 }} /><Menu.Portal><Menu.Content className="orca-menu" data-menu-family="orca" aria-label={label} align="start" collisionPadding={8} onCloseAutoFocus={(e) => { e.preventDefault(); (returnFocus ?? anchor)?.focus(); }}><MenuRows items={items} /></Menu.Content></Menu.Portal></Menu.Root>;
}
