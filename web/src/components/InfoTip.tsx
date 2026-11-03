import type { ReactNode } from 'react';
import { Popover } from 'radix-ui';
export function InfoTip({ label, children, className = '' }: { label: string; children: ReactNode; className?: string }) {
  return <Popover.Root><Popover.Trigger asChild><button type="button" className={`info-tip ${className}`} aria-label={`About ${label}`}>?</button></Popover.Trigger>
    <Popover.Portal><Popover.Content className="info-pop" side="bottom" align="start" sideOffset={4} collisionPadding={8} onKeyDown={(e) => e.stopPropagation()}>{children}</Popover.Content></Popover.Portal></Popover.Root>;
}
