import { useRef } from 'react';
import type { LayerRec } from '../preview/format';

export interface SliderTick {
  layer: number;
  color: string;
  title: string;
}

export function LayerSlider({ layers, lo, hi, onChange, ticks = [], onPlus }: {
  layers: LayerRec[];
  lo: number;
  hi: number;
  onChange(lo: number, hi: number): void;
  ticks?: SliderTick[];
  onPlus?(anchor: DOMRect): void;
}) {
  const track = useRef<HTMLDivElement>(null);
  const n = layers.length;
  const frac = (i: number) => (n > 1 ? i / (n - 1) : 0);
  const at = (clientY: number) => {
    const r = track.current!.getBoundingClientRect();
    const f = 1 - (clientY - r.top) / r.height;
    return Math.round(Math.max(0, Math.min(1, f)) * (n - 1));
  };
  const drag = (which: 'lo' | 'hi') => (e: React.PointerEvent) => {
    e.preventDefault();
    e.stopPropagation();
    const el = e.currentTarget as HTMLElement;
    el.setPointerCapture(e.pointerId);
    const move = (ev: PointerEvent) => {
      const i = at(ev.clientY);
      if (which === 'lo') onChange(Math.min(i, hi), hi);
      else onChange(lo, Math.max(i, lo));
    };
    const up = () => {
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', up);
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
  };
  const label = (i: number) => (
    <>
      <b>{i + 1}</b>
      <span>{layers[i]?.z.toFixed(2)}</span>
    </>
  );
  return (
    <div
      className="layer-slider"
      onWheel={(e) => {
        const d = e.deltaY < 0 ? 1 : -1;
        onChange(lo, Math.max(lo, Math.min(n - 1, hi + d)));
      }}
    >
      <div className="ls-label top">{label(n - 1)}</div>
      <div
        className="ls-track"
        ref={track}
        onPointerDown={(e) => {
          const i = at(e.clientY);
          if (Math.abs(i - hi) <= Math.abs(i - lo)) onChange(lo, Math.max(i, lo));
          else onChange(Math.min(i, hi), hi);
        }}
      >
        <div className="ls-fill" style={{ bottom: `${frac(lo) * 100}%`, top: `${(1 - frac(hi)) * 100}%` }} />
        {ticks.map((t) => (
          <div
            key={`${t.layer}-${t.title}`}
            className="ls-tick"
            style={{ bottom: `${frac(t.layer) * 100}%`, background: t.color }}
            title={t.title}
            onPointerDown={(e) => {
              e.stopPropagation();
              onChange(Math.min(lo, t.layer), t.layer);
            }}
          />
        ))}
        <div className="ls-thumb hi" style={{ bottom: `${frac(hi) * 100}%` }} onPointerDown={drag('hi')} title="Upper layer (Up / Down keys)">
          <div className="ls-tip">{label(hi)}</div>
          {onPlus && (
            <button
              type="button"
              className="ls-plus"
              title={ticks.some((t) => t.layer === hi) ? 'Edit custom G-code of this layer' : 'Add pause, custom G-code or color change'}
              onPointerDown={(e) => e.stopPropagation()}
              onClick={(e) => onPlus((e.currentTarget as HTMLElement).getBoundingClientRect())}
            >
              {ticks.some((t) => t.layer === hi) ? '✎' : '+'}
            </button>
          )}
        </div>
        <div className="ls-thumb lo" style={{ bottom: `${frac(lo) * 100}%` }} onPointerDown={drag('lo')} title="Lower layer">
          <div className="ls-tip">{label(lo)}</div>
        </div>
      </div>
      <div className="ls-label bottom">{label(0)}</div>
    </div>
  );
}

export function MoveSlider({ count, pos, gcodeLine, onChange }: { count: number; pos: number; gcodeLine: number; onChange(p: number | null): void }) {
  return (
    <div className="move-slider">
      <input
        type="range"
        min={1}
        max={count}
        value={pos}
        onChange={(e) => {
          const v = Number(e.target.value);
          onChange(v >= count ? null : v);
        }}
        title="Moves of the top layer (Left / Right keys)"
      />
      <span className="ms-label">
        {pos} / {count} · line {gcodeLine}
      </span>
    </div>
  );
}
