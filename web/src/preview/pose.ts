import type { PreviewData } from './data';


export function toolAxis(d: PreviewData, i: number): [number, number, number] {
  if (!d.orientation.length || i < 0 || i >= d.loaded) return [0, 0, 1];
  const k = Math.PI / 180, a = d.orientation[i * 3] * k, b = d.orientation[i * 3 + 1] * k, c = d.orientation[i * 3 + 2] * k;
  if (d.header?.tableAC) return [Math.sin(a) * Math.sin(c), Math.sin(a) * Math.cos(c), Math.cos(a)];
  if (d.header?.tableRotary) return [-Math.sin(b) * Math.cos(c), Math.sin(b) * Math.sin(c), Math.cos(b)];
  return [Math.cos(c) * Math.sin(b) * Math.cos(a) + Math.sin(c) * Math.sin(a),
    Math.sin(c) * Math.sin(b) * Math.cos(a) - Math.cos(c) * Math.sin(a), Math.cos(b) * Math.cos(a)];
}
