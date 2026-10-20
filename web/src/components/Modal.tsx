import { useLayoutEffect, useRef, useSyncExternalStore, type ReactNode } from 'react';
import { createPortal } from 'react-dom';

const FOCUSABLE = 'button:not(:disabled),input:not(:disabled):not([type=hidden]),select:not(:disabled),textarea:not(:disabled),a[href],[tabindex]:not([tabindex="-1"])';
const stack: HTMLDialogElement[] = [];
window.addEventListener('keydown', (e) => {
  const top = stack[stack.length - 1];
  if (e.key === 'Escape' && top && !top.contains(document.activeElement)) top.dispatchEvent(new Event('cancel', { cancelable: true }));
});
const listeners = new Set<() => void>();
const notify = () => listeners.forEach((l) => l());
const topModal = () => stack[stack.length - 1] ?? null;
const subscribeModal = (l: () => void) => {
  listeners.add(l);
  return () => void listeners.delete(l);
};

export function Modal({ label, onClose, className = '', backdropClose = false, returnFocus, children }: {
  label: string;
  returnFocus?: string;
  onClose(): void;
  className?: string;
  backdropClose?: boolean;
  children: ReactNode;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  const close = useRef(onClose);
  close.current = onClose;
  useLayoutEffect(() => {
    const dialog = ref.current!;
    const active = document.activeElement;
    const opener = active instanceof HTMLElement && active !== document.body ? active : null;
    const trigger = opener?.closest('.menu')?.querySelector<HTMLElement>('button') ?? null;
    let unmounting = false;
    const onNativeClose = () => !unmounting && !dialog.open && close.current();
    dialog.addEventListener('close', onNativeClose);
    const wrap = (e: KeyboardEvent) => {
      if (e.key !== 'Tab' || !(e.target instanceof Element) || e.target.closest('dialog') !== dialog) return;
      const all = [...dialog.querySelectorAll<HTMLElement>(FOCUSABLE)].filter((el) => el.getClientRects().length && el.closest('dialog') === dialog);
      if (document.activeElement !== (e.shiftKey ? all[0] : all[all.length - 1])) return;
      e.preventDefault();
      (e.shiftKey ? all[all.length - 1] : all[0])?.focus();
    };
    dialog.addEventListener('keydown', wrap);
    dialog.showModal();
    dialog.querySelector<HTMLElement>('[data-autofocus]')?.focus();
    stack.push(dialog);
    notify();
    return () => {
      unmounting = true;
      dialog.removeEventListener('close', onNativeClose);
      dialog.removeEventListener('keydown', wrap);
      stack.splice(stack.indexOf(dialog), 1);
      if (dialog.open) dialog.close();
      notify();
      const back = [opener, trigger, returnFocus ? document.querySelector<HTMLElement>(returnFocus) : null].find((e) => e?.isConnected);
      back?.focus({ preventScroll: true });
    };
  }, []);
  return (
    <dialog
      ref={ref}
      className={`modal ${className}`}
      aria-label={label}
      onCancel={(e) => {
        e.preventDefault();
        e.stopPropagation();
        if (e.target === e.currentTarget) close.current();
      }}
      onKeyDown={(e) => e.stopPropagation()}
      onMouseDown={(e) => backdropClose && e.target === e.currentTarget && close.current()}
    >
      {children}
    </dialog>
  );
}

export function ModalTitle({ title, onClose }: { title: ReactNode; onClose(): void }) {
  return (
    <div className="modal-title">
      <span>{title}</span>
      <button type="button" className="modal-close" aria-label="Close" title="Close" onClick={onClose}>
        ×
      </button>
    </div>
  );
}

export function InTopLayer({ children }: { children: ReactNode }) {
  const top = useSyncExternalStore(subscribeModal, topModal);
  return top ? createPortal(<div className="top-layer">{children}</div>, top) : <>{children}</>;
}
