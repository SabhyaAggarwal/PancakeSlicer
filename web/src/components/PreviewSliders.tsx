import { useRef } from 'react';
import type { LayerRec } from '../preview/format';
import { Icon } from './Icon';

export interface SliderTick {
  layer: number;
  color: string;
  title: string;
}

export function LayerSlider({ layers, lo, hi, onChange, ticks = [], onPlus, oneLayer = false, onOneLayer }: {
  layers: LayerRec[];
  lo: number;
  hi: number;
  onChange(lo: number, hi: number): void;
  ticks?: SliderTick[];
  onPlus?(anchor: DOMRect): void;
  oneLayer?: boolean;
  onOneLayer?(): void;
}) {
  const track = useRef<HTMLDivElement>(null);
  const n = layers.length;
  const frac = (i: number) => (n > 1 ? i / (n - 1) : 0);
  const at = (clientY: number) => {
    const r = track.current!.getBoundingClientRect();
    const f = 1 - (clientY - r.top) / r.height;
    return Math.round(Math.max(0, Math.min(1, f)) * (n - 1));
  };
  const follow = (which: 'lo' | 'hi', e: React.PointerEvent) => {
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
      el.removeEventListener('pointercancel', up);
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', up);
  };
  const drag = (which: 'lo' | 'hi') => (e: React.PointerEvent) => follow(which, e);
  const keys = (which: 'lo' | 'hi') => (e: React.KeyboardEvent) => {
    const step = e.shiftKey ? 5 : 1;
    const cur = which === 'lo' ? lo : hi;
    const next = ({ ArrowUp: cur + step, ArrowRight: cur + step, ArrowDown: cur - step, ArrowLeft: cur - step,
      PageUp: cur + 10, PageDown: cur - 10, Home: which === 'lo' ? 0 : lo, End: which === 'lo' ? hi : n - 1 } as Record<string, number>)[e.key];
    if (next === undefined) return;
    e.preventDefault();
    e.stopPropagation();
    if (which === 'lo') onChange(Math.max(0, Math.min(next, hi)), hi);
    else onChange(lo, Math.min(n - 1, Math.max(next, lo)));
  };
  const aria = (which: 'lo' | 'hi') => ({
    role: 'slider',
    tabIndex: 0,
    'aria-orientation': 'vertical' as const,
    'aria-label': which === 'hi' ? 'Upper layer' : 'Lower layer',
    'aria-valuemin': which === 'hi' ? lo + 1 : 1,
    'aria-valuemax': which === 'hi' ? n : hi + 1,
    'aria-valuenow': (which === 'hi' ? hi : lo) + 1,
    'aria-valuetext': `Layer ${(which === 'hi' ? hi : lo) + 1}, ${layers[which === 'hi' ? hi : lo]?.z.toFixed(2)} mm`,
    onKeyDown: keys(which),
  });
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
          const which = Math.abs(i - hi) <= Math.abs(i - lo) ? 'hi' : 'lo';
          if (which === 'hi') onChange(lo, Math.max(i, lo));
          else onChange(Math.min(i, hi), hi);
          follow(which, e);
        }}
      >
        <div className="ls-fill" style={{ bottom: `${frac(oneLayer ? hi : lo) * 100}%`, top: `${(1 - frac(hi)) * 100}%` }} />
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
        <div className="ls-thumb hi" style={{ bottom: `${frac(hi) * 100}%` }} onPointerDown={drag('hi')} title="Upper layer (Up / Down keys)" {...aria('hi')}>
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
        {!oneLayer && (
          <div className="ls-thumb lo" style={{ bottom: `${frac(lo) * 100}%` }} onPointerDown={drag('lo')} title="Lower layer" {...aria('lo')}>
            <div className="ls-tip">{label(lo)}</div>
          </div>
        )}
      </div>
      <div className="ls-label bottom">{label(0)}</div>
      {onOneLayer && (
        <button type="button" className={`ls-one ${oneLayer ? 'active' : ''}`} aria-pressed={oneLayer} title="On/Off one layer mode of the vertical slider (L)" onClick={onOneLayer}>
          <Icon name={oneLayer ? 'one_layer_on' : 'one_layer_off'} size={24} />
        </button>
      )}
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
