



export const ownsKeys = (e: KeyboardEvent) =>
  e.target instanceof Element && !!e.target.closest('input,select,textarea,[contenteditable="true"],[role="slider"],[role="menu"],[role="listbox"],[aria-haspopup="listbox"],[role="dialog"],dialog');





export function menuKeys(e: { key: string; preventDefault(): void; stopPropagation(): void }, menu: HTMLElement | null, close: () => void, back?: HTMLElement | null): boolean {
  if (!menu) return false;
  const items = [...menu.querySelectorAll<HTMLButtonElement>('.menu-item:not(:disabled)')];
  const at = items.indexOf(document.activeElement as HTMLButtonElement);
  if (e.key === 'ArrowDown' || e.key === 'ArrowUp') items[(at + (e.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length]?.focus();
  else if (e.key === 'Home' || e.key === 'End') items[e.key === 'Home' ? 0 : items.length - 1]?.focus();
  else if (e.key === 'Escape') {
    close();
    back?.focus();
  } else return false;
  e.preventDefault();
  e.stopPropagation();
  return true;
}
