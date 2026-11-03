import { type ReactNode } from 'react';
import { Dialog } from 'radix-ui';
export function Modal({ label, onClose, className = '', backdropClose = false, returnFocus, onEscapeKeyDown, children }: { label: string; onEscapeKeyDown?(event: KeyboardEvent): void; returnFocus?: string; onClose(): void; className?: string; backdropClose?: boolean; children: ReactNode }) {
  return <Dialog.Root open onOpenChange={(open) => { if (!open) onClose(); }}><Dialog.Portal><Dialog.Overlay className="modal-overlay" /><Dialog.Content className={`modal ${className}`} aria-label={label} aria-describedby={undefined} onEscapeKeyDown={onEscapeKeyDown} onPointerDownOutside={(e) => { if (!backdropClose) e.preventDefault(); }} onCloseAutoFocus={(e) => { const back = returnFocus ? document.querySelector<HTMLElement>(returnFocus) : null; if (back) { e.preventDefault(); back.focus(); } }} onKeyDown={(e) => e.stopPropagation()}>{children}</Dialog.Content></Dialog.Portal></Dialog.Root>;
}
export function ModalTitle({ title, onClose }: { title: ReactNode; onClose(): void }) { return <div className="modal-title"><Dialog.Title asChild><span>{title}</span></Dialog.Title><button type="button" className="modal-close" aria-label="Close" title="Close" onClick={onClose}>×</button></div>; }
export function InTopLayer({ children }: { children: ReactNode }) { return <>{children}</>; }
