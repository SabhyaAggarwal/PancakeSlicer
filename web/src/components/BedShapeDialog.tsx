import { Dropdown } from './Dropdown';
import { useState } from 'react';
import { Modal, ModalTitle } from './Modal';
import type { Config } from '../api/types';
import { bedBounds, parsePoints } from '../plater/geometry';
import { bedAsset, registerBedAsset } from '../plater/bed-assets';
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
import { union, type Polygon, type Pair } from 'polygon-clipping';
import { setOption } from '../state/store';

export default function BedShapeDialog({ config, close }: { config: Config; close: () => void }) {
  const initial = parsePoints(config.printable_area);
  const bounds = bedBounds(initial);
  const [custom, setCustom] = useState(initial.map((p) => [p.x, p.y]));
  const [error, setError] = useState('');
  const [shape, setShape] = useState(initial.length === 4 && initial.every((p) => (p.x === bounds.min.x || p.x === bounds.max.x) && (p.y === bounds.min.y || p.y === bounds.max.y)) ? 'Rectangular' : initial.length === 72 && initial.every((p) => Math.abs(p.length() - initial[0].length()) < 0.01) ? 'Circular' : 'Custom');
  const [size, setSize] = useState([bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y]);
  const [origin, setOrigin] = useState([-bounds.min.x, -bounds.min.y]);
  const [diameter, setDiameter] = useState(size[0]);
  const [texture, setTexture] = useState(String(config.bed_custom_texture ?? ''));
  const [model, setModel] = useState(String(config.bed_custom_model ?? ''));
  const valid = shape === 'Rectangular' ? size.every((n) => Number.isFinite(n) && n > 0 && n <= 214700) && origin.every((n) => Number.isFinite(n) && Math.abs(n) <= 107350) : shape === 'Circular' ? Number.isFinite(diameter) && diameter > 0 && diameter <= 214700 : custom.length >= 3;
  const points = shape === 'Rectangular' ? [[-origin[0], -origin[1]], [size[0] - origin[0], -origin[1]], [size[0] - origin[0], size[1] - origin[1]], [-origin[0], size[1] - origin[1]]] :
    shape === 'Circular' ? Array.from({ length: 72 }, (_, i) => { const a = (i + 1) * Math.PI * 2 / 72; return [diameter / 2 * Math.cos(a), diameter / 2 * Math.sin(a)]; }) : custom;
  const xs = points.map((p) => p[0]), ys = points.map((p) => p[1]);
  const minX = Math.min(0, ...xs), minY = Math.min(0, ...ys), maxX = Math.max(0, ...xs), maxY = Math.max(0, ...ys);
  const scale = 230 / Math.max(maxX - minX, maxY - minY, 1);
  const px = (n: number) => 30 + (n - minX) * scale;
  const py = (n: number) => 265 - (n - minY) * scale;
  const pair = (label: string, values: number[], change: (v: number[]) => void, min: number, max: number) => <div className="bed-pair"><span>{label}</span>{['X', 'Y'].map((axis, i) => <label key={axis}>{axis} <input className="text num" aria-label={`${label} ${axis}`} type="number" min={min} max={max} step="any" value={Number.isFinite(values[i]) ? values[i] : ''} onChange={(e) => change(values.map((v, j) => i === j ? e.target.valueAsNumber : v))} /></label>)}<span>mm</span></div>;
  const asset = (title: string, value: string, set: (v: string) => void, accept: string) => <fieldset><legend>{title}</legend>
    <input className="text" aria-label={`Custom ${title.toLowerCase()}`} value={value} placeholder="None" onChange={(e) => set(e.target.value)} />
    <div className="bed-asset-buttons"><label className="side-btn">Load...<input type="file" aria-label={`Load ${title.toLowerCase()}`} accept={accept} hidden onChange={(e) => { const file = e.target.files?.[0]; if (file) set(registerBedAsset(file)); }} /></label><button className="side-btn" type="button" disabled={!value} onClick={() => set('')}>Remove</button></div>
  </fieldset>;
  const loadShape = async (file: File) => {
    try {
      const geometry = new STLLoader().parse(await file.arrayBuffer());
      try {
        const pos = geometry.getAttribute('position');
        const triangles: Polygon[] = [];
        for (let i = 0; i < pos.count; i += 3) {
          const p = [0, 1, 2].map((j) => [pos.getX(i + j), pos.getY(i + j)] as Pair);
          const area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
          if (Math.abs(area) > 1e-8) triangles.push([p]);
        }
        if (!triangles.length) throw new Error('The selected file contains no geometry.');
        let merged = union(triangles[0]);
        for (let i = 1; i < triangles.length; i += 128) merged = union(merged, ...triangles.slice(i, i + 128));
        if (merged.length !== 1) throw new Error('The selected file contains several disjointed areas. This is not supported.');
        setCustom(merged[0][0].slice(0, -1));
        setError('');
      } finally { geometry.dispose(); }
    } catch (e) { setError(e instanceof Error ? e.message : 'Error: invalid model'); }
  };
  return <Modal className="bed-dialog" label="Bed Shape" onClose={close}>
    <form onSubmit={(e) => { e.preventDefault(); if (!valid) return; setOption('machine', 'printable_area', points.map(([x, y]) => `${Number(x.toFixed(6))}x${Number(y.toFixed(6))}`)); setOption('machine', 'bed_custom_texture', texture); setOption('machine', 'bed_custom_model', model); close(); }}>
      <ModalTitle title="Bed Shape" onClose={close} />
      <div className="bed-dialog-body"><div className="bed-dialog-fields"><fieldset><legend>Shape</legend>
        <Dropdown className="combo" aria-label="Shape" value={shape} onValueChange={setShape} options={[...['Rectangular', 'Circular', 'Custom'].map((name) => ({ value: name, label: name }))]} />
        {shape === 'Rectangular' && <>{pair('Size', size, setSize, 0, 214700)}{pair('Origin', origin, setOrigin, -107350, 107350)}</>}
        {shape === 'Circular' && <label>Diameter <input className="text num" type="number" aria-label="Diameter" min="0.01" step="any" value={diameter} onChange={(e) => setDiameter(e.target.valueAsNumber)} /> mm</label>}
        {shape === 'Custom' && <><label className="side-btn">Load shape from STL...<input type="file" aria-label="Load shape from STL" hidden accept=".stl" onChange={(e) => { const file = e.target.files?.[0]; if (file) void loadShape(file); }} /></label>{error && <div role="alert">{error}</div>}</>}
      </fieldset>{asset('Texture', texture, setTexture, '.png,.svg')}{asset('Model', model, setModel, '.stl')}</div>
      <svg className="bed-2d" viewBox="0 0 300 300" role="img" aria-label="Bed shape preview">
        <defs><pattern id="bed-grid" width={10 * scale || 10} height={10 * scale || 10} patternUnits="userSpaceOnUse"><path d={`M ${10 * scale} 0 L 0 0 0 ${10 * scale}`} fill="none" className="bed-2d-grid" strokeWidth="0.5" /></pattern><clipPath id="bed-clip"><polygon points={points.map(([x, y]) => `${px(x)},${py(y)}`).join(' ')} /></clipPath></defs>
        {valid && <><polygon points={points.map(([x, y]) => `${px(x)},${py(y)}`).join(' ')} className="bed-2d-plate" />{texture && <image href={bedAsset(texture)} x={px(Math.min(...xs))} y={py(Math.max(...ys))} width={(Math.max(...xs) - Math.min(...xs)) * scale} height={(Math.max(...ys) - Math.min(...ys)) * scale} preserveAspectRatio="none" clipPath="url(#bed-clip)" />}<rect width="300" height="300" fill="url(#bed-grid)" clipPath="url(#bed-clip)" /><path d={`M ${px(0)} ${py(0)} h 24 M ${px(0)} ${py(0)} v -24`} className="bed-2d-axes" /><circle cx={px(0)} cy={py(0)} r="3" fill="#00ae42" /><text x={px(0) + 5} y={py(0) + 15} fontSize="11" className="bed-2d-label">0,0</text></>}
      </svg></div>
      <div className="bed-dialog-buttons"><button className="side-btn" type="submit" disabled={!valid}>OK</button><button className="side-btn" type="button" onClick={close}>Cancel</button></div>
    </form>
  </Modal>;
}
