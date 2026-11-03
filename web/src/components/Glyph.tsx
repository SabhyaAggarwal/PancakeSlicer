const GLYPH = {
  more: 'M3.5 8h.01M8 8h.01M12.5 8h.01',
  back: 'M10 3.5L5.5 8l4.5 4.5',
  filter: 'M2.5 3.5h11l-4.25 5v4l-2.5 1.25V8.5z',
  settings: 'M3 5h10M3 11h10M6 3.5v3M10 9.5v3',
  rail: 'M2.5 3.5h11v9h-11zM6.5 3.5v9',
  view: 'M8 2.5l5 2.75v5.5L8 13.5l-5-2.75v-5.5zM3 5.25L8 8l5-2.75M8 8v5.5',
  edit: 'M3.5 12.5l.8-3 6.9-6.9 2.2 2.2-6.9 6.9-3 .8zM9.9 3.9l2.2 2.2',
  plus: 'M8 3.5v9M3.5 8h9',
  minus: 'M3.5 8h9',
  printer: 'M2.5 13.5h11M4 13.5v-10h8v10M4 6.5h8M7 6.5v3h2v-3M6.5 11.5h3',
} as const;
export type GlyphName = keyof typeof GLYPH;

export function Glyph({ name, size = 16 }: { name: GlyphName; size?: number }) {
  return <svg width={size} height={size} viewBox="0 0 16 16" aria-hidden="true">
    <path d={GLYPH[name]} fill="none" stroke="currentColor" strokeWidth={name === 'more' ? 2.4 : 1.3} strokeLinecap="round" strokeLinejoin="round"/>
  </svg>;
}
