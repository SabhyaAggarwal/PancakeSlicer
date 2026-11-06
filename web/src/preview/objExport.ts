

import { MoveType } from './format';
import { ROLE_COLORS } from './colors';
import { visibleToolpaths } from './visible';

export function exportToolpathsObj(name: string) {
  const v = visibleToolpaths();
  if (!v) return 0;
  const { data: d, layers: [a, b] } = v;
  const last = Math.min(b, d.layerEnd.length - 1);
  const from = a > 0 ? d.layerEnd[a - 1] ?? 0 : 0, to = d.layerEnd[last] ?? d.n;
  const parts: string[] = [`# Toolpaths exported by PancakeSlicer (web), layers ${a + 1}-${last + 1}\n`];
  let lines: string[] = [], vtx = 0, segments = 0;
  for (let i = Math.max(1, from); i < to; i++) {
    if ((d.type[i] & 0x7f) !== MoveType.Extrude) continue;
    const x0 = d.pos[(i - 1) * 4], y0 = d.pos[(i - 1) * 4 + 1], z0 = d.pos[(i - 1) * 4 + 2];
    const x1 = d.pos[i * 4], y1 = d.pos[i * 4 + 1], z1 = d.pos[i * 4 + 2];
    const len = Math.hypot(x1 - x0, y1 - y0);
    if (!(len > 1e-6)) continue;
    const hw = (d.width[i] || 0.4) / 2, h = d.height[i] || 0.2;
    const nx = (-(y1 - y0) / len) * hw, ny = ((x1 - x0) / len) * hw;
    const [r, g, bl] = (ROLE_COLORS[d.role[i]] ?? [128, 128, 128]).map((c) => (c / 255).toFixed(3));
    const col = ` ${r} ${g} ${bl}`;

    for (const [x, y, z] of [[x0, y0, z0], [x1, y1, z1]] as const)
      for (const zz of [z - h, z])
        lines.push(`v ${(x + nx).toFixed(3)} ${(y + ny).toFixed(3)} ${zz.toFixed(3)}${col}\nv ${(x - nx).toFixed(3)} ${(y - ny).toFixed(3)} ${zz.toFixed(3)}${col}\n`);

    const k = vtx + 1;
    lines.push(`f ${k} ${k + 4} ${k + 6} ${k + 2}\nf ${k + 1} ${k + 3} ${k + 7} ${k + 5}\nf ${k + 2} ${k + 6} ${k + 7} ${k + 3}\nf ${k} ${k + 1} ${k + 5} ${k + 4}\nf ${k} ${k + 2} ${k + 3} ${k + 1}\nf ${k + 4} ${k + 5} ${k + 7} ${k + 6}\n`);
    vtx += 8;
    if (++segments % 20000 === 0) { parts.push(lines.join('')); lines = []; }
  }
  parts.push(lines.join(''));
  const href = URL.createObjectURL(new Blob(parts, { type: 'model/obj' }));
  const el = document.createElement('a');
  el.href = href; el.download = `${name}.obj`; document.body.appendChild(el); el.click(); el.remove();
  setTimeout(() => URL.revokeObjectURL(href), 60000);
  return segments;
}
