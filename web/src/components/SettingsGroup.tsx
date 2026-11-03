import { useSyncExternalStore } from 'react';
import { Icon } from './Icon';
import { SectionHeading } from './SectionHeading';

const expanded = new Set<string>(), listeners = new Set<() => void>();
let version = 0;
const subscribe = (listener: () => void) => { listeners.add(listener); return () => { listeners.delete(listener); }; };

export function setGroupOpen(id: string, open: boolean) {
  if (open === expanded.has(id)) return;
  if (open) expanded.add(id); else expanded.delete(id);
  version++;
  listeners.forEach((listener) => listener());
}
export const isGroupOpen = (id: string) => expanded.has(id);
export const useSettingsGroups = () => useSyncExternalStore(subscribe, () => version);

export function GroupHead({ id, title, icon, modified, open }: { id: string; title: string; icon?: string; modified: boolean; open: boolean }) {
  return (
    <SectionHeading level="sub" className="optgroup-title">
      <button type="button" className={`optgroup-head ${modified ? 'modified' : ''}`} aria-expanded={open} onClick={() => setGroupOpen(id, !open)}>
        <svg viewBox="0 0 12 12" width="12" height="12" aria-hidden><path d="M2.5 4.5 6 8l3.5-3.5" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" /></svg>
        {icon && <Icon name={icon} size={16} />}
        <span>{title}</span>
        {modified && <span className="optgroup-mark" aria-label="Modified" role="img" />}
      </button>
    </SectionHeading>
  );
}
