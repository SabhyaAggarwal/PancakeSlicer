import { Range } from './Range';
import { pauseViewer, playViewer, scrubViewer } from '../flow/viewer';
import { useStore } from '../state/store';
import { Dropdown } from './Dropdown';
import { Glyph } from './Glyph';
import { useEffect, useMemo, useRef } from 'react';
import type { PreviewData } from '../preview/data';
import { layerOfMove } from '../preview/jump';
import { shortTime } from '../preview/format';
import { ownsKeys } from '../keys';
import { getState } from '../state/store';
import { playback, usePlayback } from '../flow/playback';

const RATES = [10, 60, 300, 1800];

export function useElapsed(d: PreviewData): Float64Array {
  return useMemo(() => {
    const given = (d as { cumTime?: Float64Array }).cumTime;
    if (given && given.length >= d.loaded) return given.subarray(0, d.loaded);
    const e = new Float64Array(d.loaded);
    for (let i = 1; i < d.loaded; ++i) e[i] = e[i - 1] + d.time[i];
    return e;
  }, [d, d.loaded]);
}

function vertexAt(e: Float64Array, t: number): number {
  let lo = 1, hi = e.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (e[mid] < t) lo = mid + 1; else hi = mid;
  }
  return lo;
}

export function OrderBar({ data, at, onSeek, gcodeOpen, onGcode, oneLayer, onOneLayer }: {
  data: PreviewData;
  at: number;
  onSeek(vertex: number): void;
  gcodeOpen: boolean;
  onGcode(): void;
  oneLayer: boolean;
  onOneLayer(): void;
}) {
  const active = useStore(s => s.tab === 'preview');
  const e = useElapsed(data);
  const last = Math.max(1, data.loaded - 1);
  const state = usePlayback();
  const playing = state.value === 'playing', rate = state.context.rate;
  useEffect(() => { if (!active) pauseViewer(); }, [active, playback]);
  const clock = useRef<{ t: number; wall: number } | null>(null);
  const atRef = useRef(at);
  atRef.current = at;
  useEffect(() => {
    if (!playing) return;
    if (atRef.current >= last) onSeek(1);
    clock.current = null;
    let frame = 0;
    const tick = (wall: number) => {
      const c = clock.current ?? (clock.current = { t: e[Math.min(atRef.current, e.length - 1)] ?? 0, wall });
      const v = vertexAt(e, c.t + ((wall - c.wall) / 1000) * rate);
      const next = Math.min(v, last);
      playback.send({type: 'POSITION', position: next, layer: layerOfMove(data, next) ?? 0});
      onSeek(next);
      if (v >= last) return playback.send({ type: 'END' });
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playing, rate, e, last, playback]);
  useEffect(() => {
    const onKey = (ev: KeyboardEvent) => {
      if (ev.repeat || ev.key !== ' ' || ownsKeys(ev) || ev.ctrlKey || ev.metaKey || ev.altKey || getState().tab !== 'preview') return;
      if (ev.target instanceof HTMLElement && ev.target.closest('button, input, select, textarea')) return;
      ev.preventDefault();
      playing ? pauseViewer() : playViewer();
    };
    const pause = () => playback.send({ type: 'PAUSE' });
    const visibility = () => { if (document.hidden) pause(); };
    document.addEventListener('visibilitychange', visibility);
    const escape = (ev: KeyboardEvent) => { if (ev.key === 'Escape') pause(); };
    window.addEventListener('keydown', onKey);
    window.addEventListener('keydown', escape);
    window.addEventListener('popstate', pause);
    window.addEventListener('hashchange', pause);
    window.addEventListener('pagehide', pause);
    return () => {
      window.removeEventListener('keydown', onKey); window.removeEventListener('keydown', escape);
      document.removeEventListener('visibilitychange', visibility);
      window.removeEventListener('popstate', pause); window.removeEventListener('hashchange', pause);
      window.removeEventListener('pagehide', pause);
    };
  }, [playing]);
  const seek = (next: number) => { scrubViewer(next, layerOfMove(data, next) ?? 0); onSeek(next); };
  const step = (d: number) => seek(Math.max(1, Math.min(last, at + d)));
  const k = layerOfMove(data, at);
  const total = e[e.length - 1] ?? 0;
  return (
    <div className="order-bar" role="group" aria-label="Playback">
      <button type="button" className="ob-btn ob-step" aria-label="Previous move" onClick={() => step(-1)}><Glyph name="prev" /></button>
      <button type="button" className="ob-btn ob-play" aria-label={playing ? 'Pause' : 'Play'} aria-pressed={playing} onClick={() => playing ? pauseViewer() : playViewer()}>
        <Glyph name={playing ? 'pause' : 'play'} />
      </button>
      <button type="button" className="ob-btn ob-step" aria-label="Next move" onClick={() => step(1)}><Glyph name="next" /></button>
      <Range min={1} max={last} value={Math.min(at, last)} aria-label="Print order" onValueChange={seek} />
      <span className="ob-readout">
        <span className="ob-time">{shortTime(e[Math.min(at, e.length - 1)] ?? 0)} / {shortTime(total)}</span>
        <span className="ob-layer"> · Layer {k == null ? '–' : k + 1} / {data.layers.length}</span>
      </span>
      <Dropdown className="ob-rate" aria-label="Playback speed" value={rate} onValueChange={(next) => playback.send({ type: 'RATE', rate: Number(next) })} options={[...RATES.map((r) => ({ value: r, label: <>{r}×</> }))]} />
      <span className="ob-sep" aria-hidden />
      <button type="button" className="ob-btn ob-one" aria-label="Show one layer (L)" aria-pressed={oneLayer} onClick={onOneLayer}><Glyph name="layers" /></button>
      <button type="button" className="ob-btn ob-gcode" aria-label="G-code viewer (C)" aria-pressed={gcodeOpen} onClick={onGcode}><Glyph name="code" /></button>
    </div>
  );
}
