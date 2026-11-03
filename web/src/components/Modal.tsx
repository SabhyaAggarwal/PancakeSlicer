import { type ReactNode } from 'react';
import { Glyph } from './Glyph';
import { Dialog } from 'radix-ui';
import './dialog.css';

export type ModalSize = 's' | 'm' | 'l' | 'xl' | 'window';
const FIELD = ':is(input:not([type=hidden], [type=checkbox], [type=radio]), textarea, select):not(:disabled, [aria-hidden=true], [tabindex="-1"])';
function focusFirst(root: HTMLElement) {
  const footer = root.querySelector('.modal-buttons');
  const primary = footer?.querySelector<HTMLElement>('.btn-primary:not(:disabled), .btn-danger:not(:disabled)');
  const target = root.querySelector<HTMLElement>('[data-autofocus]')
    ?? [...root.querySelectorAll<HTMLElement>(FIELD)].find((el) => !el.closest('.modal-title, .modal-buttons') && el.offsetParent !== null)
    ?? (primary?.classList.contains('btn-danger') ? footer?.querySelector<HTMLElement>('button:not(.btn-danger):not(:disabled)') : primary)
    ?? root;
  target.focus({ preventScroll: true });
}

export function Modal({ label, onClose, className = '', size = 'window', alert = false, backdropClose = false, returnFocus, onEscapeKeyDown, children }: {
  label: string; onClose(): void; className?: string; size?: ModalSize; alert?: boolean; backdropClose?: boolean; returnFocus?: string;
  onEscapeKeyDown?(event: KeyboardEvent): void; children: ReactNode;
}) {
  return <Dialog.Root open onOpenChange={(open) => { if (!open) onClose(); }}><Dialog.Portal><Dialog.Overlay className="modal-overlay" />
    <Dialog.Content className={`modal ${className}`} data-size={size} role={alert ? 'alertdialog' : 'dialog'} aria-label={label} aria-describedby={undefined}
      onOpenAutoFocus={(e) => { e.preventDefault(); focusFirst(e.currentTarget as HTMLElement); }} onEscapeKeyDown={onEscapeKeyDown}
      onPointerDownOutside={(e) => { if (!backdropClose) e.preventDefault(); }}
      onCloseAutoFocus={(e) => { const back = returnFocus ? document.querySelector<HTMLElement>(returnFocus) : null; if (back) { e.preventDefault(); back.focus(); } }}
      onKeyDown={(e) => e.stopPropagation()}>{children}</Dialog.Content></Dialog.Portal></Dialog.Root>;
}
export function ModalTitle({ title, onClose }: { title: ReactNode; onClose(): void }) {
  return <div className="modal-title"><Dialog.Title asChild><h2>{title}</h2></Dialog.Title><button type="button" className="modal-close" aria-label="Close" onClick={onClose}><Glyph name="close" size={14} /></button></div>;
}
export function ModalBody({ className = '', children }: { className?: string; children: ReactNode }) { return <div className={`modal-body ${className}`}>{children}</div>; }
export function ModalFooter({ lead, children }: { lead?: ReactNode; children: ReactNode }) {
  return <div className="modal-buttons">{lead && <div className="modal-lead">{lead}</div>}{children}</div>;
}
export function InTopLayer({ children }: { children: ReactNode }) { return <>{children}</>; }
