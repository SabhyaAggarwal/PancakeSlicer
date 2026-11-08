import type { ReactNode } from 'react';

export function SectionHeading({level, children, className = ''}: {level: 'main' | 'sub'; children: ReactNode; className?: string}) {
  const Heading = level === 'main' ? 'h2' : 'h3';
  return <Heading className={`section-${level} ${className}`}>{children}</Heading>;
}
