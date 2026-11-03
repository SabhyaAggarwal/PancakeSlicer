import type { ReactNode } from 'react';
import { Tabs } from 'radix-ui';
export function Group({children}: {children: ReactNode}) { return <div className="profile-fields">{children}</div>; }
export function Row({label, title, children}: {label: ReactNode; title?: string; children: ReactNode}) { return <div className="profile-field" title={title}><span>{label}</span><span>{children}</span></div>; }
export function SectionRule({title}: {title: ReactNode; count?: number}) { return <div className="profile-heading">{title}</div>; }
export function Segmented({label, value, options, onChange}: {label: string; value: string; options: {id: string; label: string; disabled?: boolean; reason?: string}[]; onChange(id: string): void}) { return <Tabs.Root value={value} onValueChange={onChange}><Tabs.List className="seg" aria-label={label}>{options.map(o => <Tabs.Trigger key={o.id} value={o.id} disabled={o.disabled} title={o.reason}>{o.label}</Tabs.Trigger>)}</Tabs.List></Tabs.Root>; }
