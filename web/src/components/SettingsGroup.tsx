import { useSyncExternalStore, type ReactNode } from 'react';
import { Icon } from './Icon';
import { SectionHeading } from './SectionHeading';

const expanded = new Set<string>(), folded = new Set<string>(), listeners = new Set<() => void>();
let version = 0;
const subscribe = (listener: () => void) => { listeners.add(listener); return () => { listeners.delete(listener); }; };

export function setGroupOpen(id: string, open: boolean) {
  if (open === expanded.has(id) && open !== folded.has(id)) return;
  if (open) { expanded.add(id); folded.delete(id); } else { expanded.delete(id); folded.add(id); }
  version++;
  listeners.forEach((listener) => listener());
}
export const isGroupOpen = (id: string, byDefault = false) => expanded.has(id) || (byDefault && !folded.has(id));
export const useSettingsGroups = () => useSyncExternalStore(subscribe, () => version);

export function GroupHead({ id, title, icon, modified, open, aside, fixed = false }: { id: string; title: string; icon?: string; modified: boolean; open: boolean; aside?: ReactNode; fixed?: boolean }) {
  const content = <>
    {fixed ? <span className="optgroup-chevron" aria-hidden /> : <svg viewBox="0 0 12 12" width="12" height="12" aria-hidden><path d="M2.5 4.5 6 8l3.5-3.5" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" /></svg>}
    {icon && <Icon name={icon} size={16} />}
    <span>{title}</span>
    {modified && <span className="field-label-hidden">(changed)</span>}
  </>;
  const head = (
    <SectionHeading level="sub" className="optgroup-title">
      {fixed ? <div className={`optgroup-head fixed ${modified ? 'modified' : ''}`}>{content}</div>
        : <button type="button" className={`optgroup-head ${modified ? 'modified' : ''}`} aria-expanded={open} onClick={() => setGroupOpen(id, !open)}>{content}</button>}
    </SectionHeading>
  );
  return aside ? <div className="optgroup-headrow">{head}<div className="optgroup-aside">{aside}</div></div> : head;
}
