import { Range } from './Range';
import { useSyncExternalStore } from 'react';
import { alignSection, sectionView, setSection, subscribeSection } from '../plater/section';
import { SURFACE_LEGEND, toggleTopSurfaces, useSurfaceSource, useTopSurfaces } from '../top-surfaces';
import { IconButton } from './Icon';

let lastRatio = 0.5;

export default function CanvasTools() {
  const s = useSyncExternalStore(subscribeSection, sectionView);
  const active = s.ratio > 0;
  const surfaces = useTopSurfaces();
  const from = useSurfaceSource();
  const toggle = () => { if (active) { lastRatio = s.ratio; setSection({ ratio: 0 }); } else setSection({ ratio: lastRatio }); };
  return (
    <div className="canvas-tools">
      {s.open && (
        <div className="canvas-section-panel">
          <span>Section view</span>
          <Range min={0} max={1} step={0.01} value={s.ratio} aria-label="Section view" onValueChange={(next) => setSection({ ratio: next })} />
          <input className="text num" type="number" min={0} max={1} step={0.05} value={s.ratio.toFixed(2)} aria-label="Section view ratio" onChange={(e) => setSection({ ratio: Number(e.target.value) || 0 })} />
          <button type="button" className="btn small" onClick={alignSection}>Set viewing angle</button>
          <IconButton icon="revert_btn" size={14} title="Reset" disabled={!active} onClick={() => setSection({ ratio: 0 })} />
        </div>
      )}
      <span onContextMenu={(e) => { e.preventDefault(); toggle(); }} onWheel={(e) => setSection({ ratio: s.ratio + (e.deltaY > 0 ? -0.01 : 0.01) })}
        onAuxClick={(e) => e.button === 1 && alignSection()}>
        <IconButton icon={active ? 'canvas_section_active' : 'canvas_section'} size={28} title="Section view" active={s.open} onClick={() => setSection({ open: !s.open })} />
      </span>
      <span className="canvas-surfaces">
        <IconButton icon={surfaces === 'off' ? 'canvas_top_surfaces' : 'canvas_top_surfaces_active'} size={28} title="Top surfaces" active={surfaces !== 'off'}
          className={surfaces === 'loading' ? 'loading' : ''} onClick={() => toggleTopSurfaces()} />
        {surfaces !== 'off' && (
          <span className="canvas-surface-legend" role="status">
            {SURFACE_LEGEND.map(([colour, label]) => <span key={label}><i style={{ background: colour }} />{label}</span>)}
            <span className="canvas-surface-source">{from === 'slice' ? 'From the slice' : from === 'plan' ? 'Plan until sliced' : 'Estimate until sliced'}</span>
          </span>
        )}
      </span>
      <IconButton icon="canvas_zoom" size={28} title="Fit camera to scene or selected object." onClick={() => window.dispatchEvent(new CustomEvent('camera-fit'))} />
    </div>
  );
}
