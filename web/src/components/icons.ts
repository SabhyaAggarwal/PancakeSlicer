


const line = (x1: number, y1: number, x2: number, y2: number, c = 'g') => `<line class="${c}" x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}"/>`;
const path = (d: string, c = 't', w = 1.3) => `<path class="${c}" d="${d}" stroke-width="${w}"/>`;
const base = line(2, 21.5, 22, 21.5);
function stairs(top: number) {
  const w = [18, 16, 14, 11, 7];
  let d = '';
  for (let i = 0; i < 5; i++) {
    const y = 19.5 - i * 2.6;
    if (y < top) break;
    d += line(12 - w[i] / 2, y, 12 + w[i] / 2, y);
  }
  return d;
}
const arc = (dy: number) => path(`M3 ${19.5 + dy} Q12 ${3 + dy} 21 ${19.5 + dy}`);
const rows = (f: (i: number) => string) => [0, 1, 2, 3, 4].map(f).join('');

const ICONS = {
  layers: () => base + rows((i) => path(`M3 ${20 - i * 0.6} Q12 ${20 - i * 3.6} 21 ${20 - i * 0.6}`, 't', 1)),
  standard: () => base + stairs(4),
  smooth: () => base + stairs(12) + arc(-0.5) + arc(2.4),
  zaa: () => base + stairs(4) + path('M3.5 19.3 L5 16.9 L7 14.3 L9.4 12 L12 11 L14.6 12 L17 14.3 L19 16.9 L20.5 19.3'),
  overhang: () => base + line(6, 21.5, 6, 5) + line(9, 21.5, 9, 8) + path('M9 8 Q13 8 21 5') + path('M9 10.5 Q14 10 21 7.5'),
  walls: () => base + rows((i) => line(5, 19.5 - i * 3, 14, 19.5 - i * 3) + line(16, 19.5 - i * 3, 19, 19.5 - i * 3, 't') + line(16, 18 - i * 3, 19, 18 - i * 3, 't')),
  bridge: () => base + line(4, 21.5, 4, 12) + line(20, 21.5, 20, 12) + path('M4 12 Q12 7 20 12') + path('M4 9.6 Q12 4.6 20 9.6'),
  chunks: () => base + line(5, 21.5, 5, 12) + line(8, 21.5, 8, 12) + [0, 1, 2].map((i) => line(9 + i * 3, 12 - i * 2, 15 + i * 3, 18 - i * 2, 't')).join(''),
  rotary: () => '<circle class="g" cx="12" cy="17" r="4.5"/>' + path('M7 17 A5 5 0 0 1 17 17') + path('M16 3 L20 7 L17.5 9.5 L13.5 5.5 Z', 'g', 1) + line(15.5, 7.5, 13, 11),
  vase: () => base + path('M9 21.5 Q8.5 13 3 6') + path('M15 21.5 Q15.5 13 21 6'),
  skin: () => base + path('M5 21.5 L5 10 L12 4', 'g', 1) + line(6.5, 9.2, 12.5, 4.2, 't') + line(7.4, 10.4, 13.4, 5.4, 't'),
};
export type IconId = keyof typeof ICONS;

const cache = new Map<IconId, string>();

export function styleIcon(id: IconId, size = 24) {
  if (!cache.has(id)) cache.set(id, ICONS[id]());
  return `<svg class="ps-icon" width="${size}" height="${size}" viewBox="0 0 24 24" aria-hidden="true" focusable="false">${cache.get(id)}</svg>`;
}
