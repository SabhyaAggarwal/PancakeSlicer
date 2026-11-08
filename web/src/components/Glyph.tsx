const GLYPH = {
  fullscreen: 'M6 2.5H2.5V6M10 2.5h3.5V6M2.5 10v3.5H6M13.5 10v3.5H10',
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
  trash: 'M3 4.5h10M6.5 4.5V3h3v1.5M4.5 4.5l.6 8.5h5.8l.6-8.5M7 7v4M9 7v4',
  undo: 'M4.5 6.5h5.25a2.75 2.75 0 0 1 0 5.5H6.5M4.5 6.5 7 4M4.5 6.5 7 9',
  play: 'M5 3.5v9l7.5-4.5z',
  pause: 'M5.5 3.5v9M10.5 3.5v9',
  prev: 'M11 4L6 8l5 4M4.5 4v8',
  next: 'M5 4l5 4-5 4M11.5 4v8',
  layers: 'M8 2.5l5.5 3L8 8.5l-5.5-3zM2.5 8.5L8 11.5l5.5-3M2.5 11L8 14l5.5-3',
  code: 'M5.5 4.5L2 8l3.5 3.5M10.5 4.5L14 8l-3.5 3.5M9 3l-2 10',
  close: 'M4 4l8 8M12 4l-8 8',
  update: 'M14.5 8a6.5 6.5 0 1 1-13 0 6.5 6.5 0 0 1 13 0zM8 11.25v-6.5M5.25 7.5 8 4.75l2.75 2.75',
} as const;
export type GlyphName = keyof typeof GLYPH;

export function Glyph({ name, size = 16 }: { name: GlyphName; size?: number }) {
  return <svg width={size} height={size} viewBox="0 0 16 16" aria-hidden="true">
    <path d={GLYPH[name]} fill={name === 'play' ? 'currentColor' : 'none'} stroke="currentColor" strokeWidth={name === 'more' ? 2.4 : 1.3} strokeLinecap="round" strokeLinejoin="round"/>
  </svg>;
}
