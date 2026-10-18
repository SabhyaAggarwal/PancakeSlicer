import { slicingText } from '../api/progress';
import { useEffect, useMemo, useReducer, useRef, useState } from 'react';
import { effectiveConfig, getState, printerPreset, useStore } from '../state/store';
import { computeRanges, hexToRgb, type NonplanarColor, type Ranges, type ViewType, type Visibility } from '../preview/colors';
import { loadMoves, type PreviewData } from '../preview/data';
import { MoveType } from '../preview/format';
import { NonplanarHover, explain, type HoverInfo } from '../preview/hover';
import { PreviewScene } from '../preview/PreviewScene';
import { Legend } from './PreviewLegend';
import { LayerSlider, MoveSlider } from './PreviewSliders';
import { EventList, EventMenu, eventTicks } from './LayerEvents';

let current: { key: string; data: PreviewData; cancel(): void } | null = null;
let live: { header(d: PreviewData): void; chunk(d: PreviewData): void; done(d: PreviewData): void; error(d: PreviewData): void } | null = null;

function source(): { key: string; url?: string; file?: File } | null {
  const q = new URLSearchParams(location.search).get('moves');
  if (q) return { key: `url:${q}`, url: q };
  const s = getState();
  if (s.job.state === 'done' && s.job.id && s.api) return { key: `job:${s.job.id}`, url: s.api.movesUrl(s.job.id) };
  return null;
}

export interface PreviewUi {
  view: ViewType;
  nonplanar: NonplanarColor;
  vis: Visibility;
  shells: boolean;
}

export function PreviewView() {
  const job = useStore((s) => s.job);
  const objects = useStore((s) => s.objects);
  const printer = useStore((s) => s.printer);
  const machines = useStore((s) => s.machines);
  const host = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<PreviewScene | null>(null);
  const [, bump] = useReducer((x: number) => x + 1, 0);
  const [dropped, setDropped] = useState<File | null>(null);
  const [data, setData] = useState<PreviewData | null>(current?.data ?? null);
  const [ui, setUi] = useState<PreviewUi>({
    view: 'feature',
    nonplanar: 'shell',
    vis: { travel: false, wipe: false, retract: false, unretract: false, seam: true, hiddenRoles: new Set() },
    shells: false,
  });
  const [layers, setLayers] = useState<[number, number]>([0, Number.MAX_SAFE_INTEGER]);
  const [movePos, setMovePos] = useState<number | null>(null);
  const ranges = useRef<Ranges | null>(null);
  const hover = useRef(new NonplanarHover());
  const drawRange = useRef<[number, number]>([1, 1]);
  const [tip, setTip] = useState<(HoverInfo & { x: number; y: number }) | null>(null);
  const lastRanges = useRef(0);
  const events = useStore((s) => s.events);
  const [eventMenu, setEventMenu] = useState<{ anchor: DOMRect; layer: number } | null>(null);

  useEffect(() => {
    const scene = new PreviewScene(host.current!);
    sceneRef.current = scene;
    let lastBump = 0;
    scene.onFrame = () => {
      if (performance.now() - lastBump > 500) {
        lastBump = performance.now();
        bump();
      }
    };
    return () => {
      scene.dispose();
      sceneRef.current = null;
    };
  }, []);

  const machineEdits = useStore((s) => s.dirty.machine);
  const machine = useMemo(() => printerPreset(getState()), [printer, machines, machineEdits]);
  useEffect(() => sceneRef.current?.setBed(machine), [machine]);
  useEffect(() => sceneRef.current?.setShells(objects, ui.shells), [objects, ui.shells]);

  const colorCtx = (d: PreviewData) => {
    const s = getState();
    const extruderColors = (d.meta.extruder_colors?.length ? d.meta.extruder_colors : s.filamentSlots.map((_, i) => {
      const c = effectiveConfig(s, 'filament', i).filament_colour;
      return Array.isArray(c) ? String(c[0]) : String(c ?? '');
    })).map(hexToRgb);
    return {
      view: ui.view,
      nonplanar: ui.nonplanar,
      ranges: ranges.current!,
      extruderColors,
      colorPrintColors: (d.meta.color_print_colors ?? []).map(hexToRgb),
    };
  };
  const recolor = (d: PreviewData) => {
    if (ui.view === 'nonplanar' && ui.nonplanar === 'slope') d.computeSlope();
    ranges.current = computeRanges(d, ui.vis);
    lastRanges.current = performance.now();
    sceneRef.current?.setColors(colorCtx(d));
  };

  const handlers: NonNullable<typeof live> = {
    header: (d) => {
      sceneRef.current?.setData(d);
      setData(d);
      ranges.current = computeRanges(d, ui.vis);
      bump();
    },
    chunk: (d) => {
      if (!ranges.current || performance.now() - lastRanges.current > 500) recolor(d);
      sceneRef.current?.sync();
      bump();
    },
    done: (d) => {
      recolor(d);
      bump();
    },
    error: (d) => {
      setData(d);
      bump();
    },
  };
  live = handlers;
  useEffect(() => {
    live = handlers;
    return () => {
      if (live === handlers) live = null;
    };
  });
  useEffect(() => setDropped(null), [job.id]);
  useEffect(() => {
    const src = dropped ? { key: `file:${dropped.name}:${dropped.size}`, file: dropped } : source();
    if (!src) return;
    if (current?.key === src.key) {
      const d = current.data;
      const scene = sceneRef.current;
      if (d.header && scene && !scene.holds(d)) {
        scene.setData(d);
        recolor(d);
        scene.sync();
      }
      return;
    }
    current?.cancel();
    const mine = (d: PreviewData) => current?.data === d;
    const handle = loadMoves(src, {
      header: (d) => mine(d) && live?.header(d),
      chunk: (d) => mine(d) && live?.chunk(d),
      done: (d) => mine(d) && live?.done(d),
      error: (d) => mine(d) && live?.error(d),
    });
    current = { key: src.key, ...handle };
    setData(null);
    setLayers([0, Number.MAX_SAFE_INTEGER]);
    setMovePos(null);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [job.id, job.state, dropped]);

  useEffect(() => {
    if (!data?.header) return;
    recolor(data);
    bump();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ui.view, ui.nonplanar, ui.vis]);

  const layerCount = data?.layers.length ?? 0;
  const lo = Math.min(layers[0], Math.max(0, layerCount - 1));
  const hi = Math.min(layers[1], Math.max(0, layerCount - 1));
  const [topFirst, topEnd] = data && layerCount ? data.layerSpan(hi, hi) : [1, 1];
  const topMoves = Math.max(0, topEnd - topFirst);
  const pos = movePos == null ? topMoves : Math.min(movePos, topMoves);
  useEffect(() => {
    const scene = sceneRef.current;
    if (!scene || !data) return;
    const [s0] = layerCount ? data.layerSpan(lo, hi) : [1];
    const s1 = topFirst + pos;
    drawRange.current = [s0, s1];
    const v = ui.vis;
    let roleMask = 0;
    for (let r = 0; r < 20; ++r) if (!v.hiddenRoles.has(r)) roleMask |= 1 << r;
    let typeMask = 1 << MoveType.Extrude;
    if (v.travel) typeMask |= 1 << MoveType.Travel;
    if (v.wipe) typeMask |= 1 << MoveType.Wipe;
    let optionMask = 0;
    if (v.retract) optionMask |= 1 << MoveType.Retract;
    if (v.unretract) optionMask |= 1 << MoveType.Unretract;
    if (v.seam) optionMask |= 1 << MoveType.Seam;
    scene.setDrawState({ range: [s0, s1], roleMask, typeMask, optionMask }, pos < topMoves ? s1 - 1 : null);
  }, [data, data?.loaded, data?.done, lo, hi, pos, topFirst, topMoves, ui.vis, layerCount]);

  (window as unknown as { __preview: unknown }).__preview = {
    scene: sceneRef.current,
    data: current?.data,
    setLayers: (a: number, b: number) => { setLayers([a, b]); setMovePos(null); },
    setMove: (p: number | null) => setMovePos(p),
    setView: (view: ViewType, nonplanar?: NonplanarColor) => setUi((u) => ({ ...u, view, nonplanar: nonplanar ?? u.nonplanar })),
    setVis: (v: Partial<Visibility>, shells?: boolean) => setUi((u) => ({ ...u, vis: { ...u.vis, ...v }, shells: shells ?? u.shells })),
  };

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement)?.closest('input,select,textarea') || getState().tab !== 'preview') return;
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
        const d = e.key === 'ArrowUp' ? 1 : -1;
        setLayers(([a, b]) => [a, Math.max(a, Math.min(layerCount - 1, Math.min(b, layerCount - 1) + d))]);
        setMovePos(null);
      } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
        const d = e.key === 'ArrowRight' ? 1 : -1;
        setMovePos((p) => Math.max(1, Math.min(topMoves, (p ?? topMoves) + d)));
      }
      else return;
      e.preventDefault();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [layerCount, topMoves]);

  const loading = data && !data.done && !data.error;
  const status = job.state === 'running' || job.state === 'submitting'
    ? slicingText(job)
    : !data && !source() && !dropped
    ? job.state === 'done' ? null : 'No sliced result. Slice the plate to see the result here.'
    : data?.error
      ? /^404 /.test(data.error)
        ? 'This slicing service did not produce preview data for the job. The sliced file can still be exported.'
        : `Cannot load the toolpaths: ${data.error}`
      : null;

  return (
    <div
      className="preview"
      onDragOver={(e) => e.preventDefault()}
      onDrop={(e) => {
        const f = e.dataTransfer.files[0];
        if (f && /\.(moves|bin|gz)$/i.test(f.name)) {
          e.preventDefault();
          e.stopPropagation();
          setDropped(f);
        }
      }}
    >
      <div
        ref={host}
        className="preview-canvas"
        onPointerLeave={() => setTip(null)}
        onPointerMove={(e) => {
          const scene = sceneRef.current;
          if (ui.view !== 'nonplanar' || e.buttons || !scene || !data?.states) return tip && setTip(null);
          const r = host.current!.getBoundingClientRect();
          const x = e.clientX - r.left, y = e.clientY - r.top;
          const id = hover.current.pick(data, scene.camera, r.width, r.height, drawRange.current, ui.vis.hiddenRoles, x, y);
          setTip(id < 0 ? null : { ...explain(data, id, job.status?.record), x, y });
        }}
      />
      {tip && (
        <div className="preview-tip" style={{ left: tip.x + 16, top: tip.y + 16 }}>
          <b>{tip.title}</b>
          <div>{tip.body}</div>
        </div>
      )}
      {status && <div className="preview-status">{status}</div>}
      {data?.header && (
        <Legend
          data={data}
          ui={ui}
          setUi={setUi}
          ranges={ranges.current}
          range={layerCount ? data.layerSpan(lo, hi) : [1, 1]}
          scene={sceneRef.current}
        />
      )}
      {loading && (
        <div className="preview-loading">
          Loading toolpaths… {data!.n ? Math.round((100 * data!.loaded) / data!.n) : 0}% ({(data!.loaded / 1e6).toFixed(2)} of {(data!.n / 1e6).toFixed(2)} M moves)
        </div>
      )}
      {layerCount > 0 && (
        <LayerSlider
          layers={data!.layers}
          lo={lo}
          hi={hi}
          onChange={(a, b) => {
            setLayers([a, b]);
            setMovePos(null);
          }}
          ticks={eventTicks(events, data!.layers)}
          onPlus={(anchor) => setEventMenu({ anchor, layer: hi })}
        />
      )}
      {layerCount > 0 && (
        <EventList
          onPick={(z) => {
            const i = data!.layers.findIndex((l) => l.z >= z - 1e-3);
            if (i >= 0) setLayers(([a]) => [Math.min(a, i), i]);
          }}
        />
      )}
      {eventMenu && data && (
        <EventMenu
          anchor={eventMenu.anchor}
          layer={eventMenu.layer}
          z={Math.round(data.layers[eventMenu.layer].z * 1e4) / 1e4}
          onClose={() => setEventMenu(null)}
        />
      )}
      {layerCount > 0 && topMoves > 0 && (
        <MoveSlider count={topMoves} pos={pos} gcodeLine={data!.gcode[topFirst + pos - 1]} onChange={setMovePos} />
      )}
    </div>
  );
}
