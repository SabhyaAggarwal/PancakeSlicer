import { Range } from './Range';
import { useSyncExternalStore } from 'react';
import { alignSection, sectionView, setSection, subscribeSection } from '../plater/section';
import { IconButton } from './Icon';

let lastRatio = 0.5;

export default function CanvasTools() {
  const s = useSyncExternalStore(subscribeSection, sectionView);
  const active = s.ratio > 0;
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
      <IconButton icon="canvas_zoom" size={28} title="Fit camera to scene or selected object." onClick={() => window.dispatchEvent(new CustomEvent('camera-fit'))} />
    </div>
  );
}
