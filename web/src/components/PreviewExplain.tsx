import { useEffect, useMemo, useReducer, useRef, useState } from 'react';
import * as THREE from 'three';
import { forgetCompare, setStandardData, startCompare, stopCompare, useCompare, useStandardData } from '../compare';
import { computeRanges, drawMasks, NONPLANAR_PLANAR_COLOR, type Visibility } from '../preview/colors';
import { loadMoves, type PreviewData } from '../preview/data';
import { area, grams, microns, readRecord, supportText, type RecordSummary } from '../preview/explain-record';
import { MoveType, shortTime } from '../preview/format';
import { PreviewScene } from '../preview/PreviewScene';
import { ERROR_BINS, surfaceGrid } from '../preview/surface-grid';
import { getState, printerPreset } from '../state/store';
import { Icon } from './Icon';
import { colorContext, type PreviewUi } from './PreviewView';

const TWO_SCENE_VERTICES = 8e6;
const NOT_PART = new Set([12, 17, 18]);
const UNCALIBRATED = 'Head sizes from drawings, not measured on this printer';

function supportGrams(d: PreviewData | null, total: number | null): number | null {
  if (!d?.done) return null;
  const mm = d.roleFilament[14] + d.roleFilament[15] + d.roleFilament[16];
  const all = d.filamentMm.reduce((a, b) => a + (b ?? 0), 0);
  return mm <= 0 ? 0 : total != null && all > 0 ? (total * mm) / all : null;
}

function partBox(d: PreviewData): [THREE.Vector3, THREE.Vector3] | null {
  const min = new THREE.Vector3(Infinity, Infinity, Infinity), max = new THREE.Vector3(-Infinity, -Infinity, -Infinity);
  const p = new THREE.Vector3();
  for (let i = 1; i < d.loaded; ++i) {
    if ((d.type[i] & 0x7f) !== MoveType.Extrude || NOT_PART.has(d.role[i])) continue;
    p.set(d.pos[i * 4], d.pos[i * 4 + 1], d.pos[i * 4 + 2]);
    min.min(p);
    max.max(p);
  }
  return min.x <= max.x ? [min, max] : null;
}

function curvedShare(s: RecordSummary): number | null {
  const total = s.topCurved + s.topFlat;
  return total > 0 ? Math.min(s.topFlat > 0 ? 99 : 100, Math.max(s.topCurved > 0 ? 1 : 0, Math.round((100 * s.topCurved) / total))) : null;
}

const time = (s: number | null) => (s == null ? '–' : shortTime(s));

export function ExplainLegend(props: {
  data: PreviewData;
  record: Record<string, unknown>;
  ui: PreviewUi;
  setUi(f: (u: PreviewUi) => PreviewUi): void;
  compare: boolean;
  setCompare(on: boolean): void;
}) {
  const { data, record, ui, setUi, compare, setCompare } = props;
  const s = useMemo(() => readRecord(record), [record]);
  const cmp = useCompare();
  const stdData = useStandardData();
  const previous = useRef<Pick<PreviewUi, 'view' | 'nonplanar'>>({ view: 'feature', nonplanar: 'shell' });
  if (!s || (!s.curved && s.mode === 'off')) return null;
  const highlight = ui.view === 'nonplanar' && ui.nonplanar === 'curved';
  const std = compare && cmp.state === 'done' ? readRecord(cmp.status.record) : null;
  const curvedPct = curvedShare(s);
  const better = (a: number | null | undefined, b: number | null | undefined) => a != null && b != null && b < a / 1.15;
  const timeDelta = std?.timeS && s.timeS ? (s.timeS - std.timeS) / std.timeS : null;
  const v = s.verifier;
  const closest = v?.closest != null && Number.isFinite(v.closest) && v.closest > 0 ? `Closest pass ${v.closest.toFixed(2)} mm` : undefined;
  const pending = !compare ? null : cmp.state === 'running' ? (cmp.queued ? 'waiting' : `${Math.round(cmp.pct)}%`) : cmp.state === 'failed' ? (cmp.busy ? 'busy' : 'failed') : null;

  type Row = [string, string, string, boolean?, string?];
  const rows: Row[] = [];
  if (s.wholePart) rows.push(['Curved layers', '', 'whole part', false, s.wholePart.source]);
  else if (s.refusedWhy) rows.push(['Curved layers', '', 'not used', false, s.refusedWhy]);
  rows.push(['Top error', std ? microns(std.topErrorMm) : pending ?? '', microns(s.topErrorMm), better(std?.topErrorMm, s.topErrorMm),
    better(std?.topErrorMm, s.topErrorMm) ? `${(std!.topErrorMm! / s.topErrorMm!).toFixed(1)}× smoother than Standard` : undefined]);
  const stdPct = std ? curvedShare(std) ?? 0 : null;
  if (curvedPct != null) rows.push(['Curved top', stdPct != null ? `${stdPct}%` : '', `${curvedPct}%`, stdPct != null && curvedPct > stdPct]);
  if (s.shells) rows.push(['Curved shells', std ? String(std.shells) : '', String(s.shells)]);
  if (s.overhangs + s.bridges) rows.push(['Curved overhangs', std ? String(std.overhangs + std.bridges) : '', String(s.overhangs + s.bridges), false, `${s.overhangs} overhangs, ${s.bridges} bridges`]);
  rows.push(['Time', std ? time(std.timeS) : '', time(s.timeS) + (timeDelta != null ? ` (${timeDelta >= 0 ? '+' : ''}${Math.round(timeDelta * 100)}%)` : '')]);
  if (s.grams != null) rows.push(['Filament', std ? grams(std.grams) : '', grams(s.grams)]);
  rows.push(['Support', std ? supportText(supportGrams(stdData, std.grams)) : '', supportText(supportGrams(data, s.grams))]);
  if (v) rows.push(['Head hits', '', String(v.hits), false, closest]);
  if (std?.curved) rows.push(['Result', 'not flat', '', false, 'The Standard slice still printed curved passes']);
  else if (std && !better(std.topErrorMm, s.topErrorMm)) {
    const faster = timeDelta != null && timeDelta > 0.01;
    rows.push(['Result', '', faster ? 'Standard faster' : 'same', false,
      faster ? 'Standard is faster here and its top is as good' : 'On this part both print the same']);
  }

  (window as unknown as { __compare: unknown }).__compare = { state: cmp, summary: s, standard: std, stdData, rows };

  return (
    <div className="np-legend">
      <div className="lg-radios">
        <label className="lg-opt"><input type="checkbox" checked={compare} onChange={(e) => setCompare(e.target.checked)} />Compare with Standard</label>
        <label className="lg-opt">
          <input type="checkbox" checked={highlight} onChange={(e) => {
            const on = e.target.checked;
            if (on) previous.current = { view: ui.view, nonplanar: ui.nonplanar };
            setUi((u) => ({ ...u, ...(on ? { view: 'nonplanar', nonplanar: 'curved' } : previous.current) }));
          }} />Highlight curved
        </label>
        {surfaceGrid(record) && (
          <label className="lg-opt">
            <input type="checkbox" checked={ui.surface} onChange={(e) => setUi((u) => ({ ...u, surface: e.target.checked }))} />Surface error
          </label>
        )}
      </div>
      {ui.surface && surfaceGrid(record) ? (
        <>
          <div className="lg-sub">Top error (µm)</div>
          {[...ERROR_BINS].reverse().map((b) => (
            <div className="lg-row" key={b.label}><span className="lg-swatch" style={{ background: `rgb(${b.color.join(',')})` }} />{b.label}</div>
          ))}
        </>
      ) : <>
      <table className="lg-table np-table">
        <thead>
          <tr>
            <th></th>
            {compare && <th>Standard</th>}
            <th>{s.style}</th>
          </tr>
        </thead>
        <tbody>
          {rows.map(([label, a, b, teal, tip]) => (
            <tr key={label} title={tip}>
              <td>{label}</td>
              {compare && <td>{a}</td>}
              <td className={teal ? 'np-good' : undefined}>{b}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {compare && cmp.state === 'running' && (
        <button className="lg-mode" onClick={() => setCompare(false)} title="Standard slices after your own slices">Cancel Standard</button>
      )}
      {compare && cmp.state === 'failed' && (
        <button className="lg-mode" title={cmp.busy ? 'The slicing service is busy' : cmp.error} onClick={() => void startCompare()}>
          {cmp.busy ? 'Service busy · Retry' : 'Standard failed · Retry'}
        </button>
      )}
      {v?.uncalibrated && (
        <div className="lg-row np-warn" title={UNCALIBRATED}><Icon name="warning" size={12} />Head model uncalibrated</div>
      )}
      {s.flat.length > 0 && (
        <>
          <div className="lg-sub">Flat areas</div>
          {s.flat.map((f) => (
            <div className="lg-row" key={f.name} title={f.reason}>
              <span className="lg-swatch" style={{ background: `rgb(${NONPLANAR_PLANAR_COLOR.join(',')})` }} />
              {f.name} {area(f.area)}
            </div>
          ))}
        </>
      )}
      </>}
    </div>
  );
}

export function StandardView(props: {
  data: PreviewData;
  record: Record<string, unknown>;
  scene: PreviewScene | null;
  ui: PreviewUi;
  zRange: [number, number];
}) {
  const { data, record, scene, ui } = props;
  const cmp = useCompare();
  const style = useMemo(() => readRecord(record)?.style ?? '', [record]);
  useEffect(() => {
    void startCompare();
  }, [record]);
  useEffect(() => () => stopCompare(), []);
  useEffect(() => {
    if (!scene || !data.done) return;
    const box = partBox(data);
    if (box) scene.fit(...box);
  }, [scene, data, data.done]);
  return (
    <>
      <StandardPane main={scene} cmp={cmp} ui={ui} vis={ui.vis} zRange={props.zRange} mainVertices={data.n} />
      <div className="cmp-label cmp-left">Standard</div>
      <div className="cmp-label cmp-right">{style}</div>
    </>
  );
}

function StandardPane(props: {
  main: PreviewScene | null;
  cmp: ReturnType<typeof useCompare>;
  ui: PreviewUi;
  vis: Visibility;
  zRange: [number, number];
  mainVertices: number;
}) {
  const { main, cmp, ui, vis, zRange } = props;
  const host = useRef<HTMLDivElement>(null);
  const [scene, setScene] = useState<PreviewScene | null>(null);
  const [data, setData] = useState<PreviewData | null>(null);
  const [tooLarge, setTooLarge] = useState(false);
  const [tip, setTip] = useState<{ text: string; x: number; y: number } | null>(null);
  const grid = ui.surface && cmp.state === 'done' ? surfaceGrid(cmp.status.record) : null;
  useEffect(() => scene?.setSurface(grid), [scene, grid]);
  const [, bump] = useReducer((x: number) => x + 1, 0);
  const id = cmp.state === 'done' ? cmp.id : null;

  useEffect(() => {
    if (!main) return;
    const sc = new PreviewScene(host.current!);
    sc.setBed(printerPreset(getState()));
    sc.copyCamera(main);
    (window as unknown as { __compareScene: unknown }).__compareScene = sc;
    let syncing = false;
    const follow = (to: PreviewScene, from: PreviewScene) => () => {
      if (syncing) return;
      syncing = true;
      to.copyCamera(from);
      syncing = false;
    };
    const a = follow(sc, main), b = follow(main, sc);
    main.controls.addEventListener('change', a);
    sc.controls.addEventListener('change', b);
    setScene(sc);
    return () => {
      main.controls.removeEventListener('change', a);
      sc.controls.removeEventListener('change', b);
      sc.dispose();
      setScene(null);
    };
  }, [main]);

  useEffect(() => {
    if (!scene || !id || cmp.state !== 'done') return;
    const api = getState().api!;
    let skip = false;
    const h = loadMoves({ url: api.movesUrl(id) }, {
      header: (d) => {
        skip = d.n + props.mainVertices > TWO_SCENE_VERTICES;
        setTooLarge(skip);
        if (skip) return;
        scene.setData(d);
        setData(d);
      },
      chunk: () => !skip && scene.sync(),
      done: (d) => {
        setStandardData(d);
        if (skip) return;
        scene.setColors(colorContext(d, ui, computeRanges(d, vis)));
        scene.sync();
        bump();
      },
      error: (d) => {
        if (/^404 /.test(d.error ?? '')) {
          forgetCompare(cmp.key);
          void startCompare();
        }
      },
    });
    return () => {
      h.cancel();
      setStandardData(null);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [scene, id]);

  useEffect(() => {
    if (scene && data?.done) scene.setColors(colorContext(data, ui, computeRanges(data, vis)));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ui.view, ui.nonplanar, vis]);

  useEffect(() => {
    if (!scene || !data?.layers.length) return;
    const L = data.layers;
    let lo = L.findIndex((l) => l.z >= zRange[0] - 1e-3);
    if (lo < 0) lo = L.length - 1;
    let hi = lo;
    while (hi + 1 < L.length && L[hi + 1].z <= zRange[1] + 1e-3) ++hi;
    scene.setDrawState({ range: data.layerSpan(lo, hi), ...drawMasks(vis, !!grid) }, null);
  }, [scene, data, data?.loaded, data?.done, zRange[0], zRange[1], vis, grid]);

  return (
    <div ref={host} className="preview-canvas cmp-std" onPointerLeave={() => setTip(null)} onPointerMove={(e) => {
      if (!grid || !scene || e.buttons) return tip && setTip(null);
      const r = host.current!.getBoundingClientRect();
      const x = e.clientX - r.left, y = e.clientY - r.top, cell = scene.pickSurface(x, y);
      setTip(cell < 0 ? null : { text: `Top error ${Math.round(grid.rms[cell] * 1000)} µm`, x, y });
    }}>
      {tooLarge && <div className="preview-status">Too large to draw twice</div>}
      {tip && <div className="preview-tip" style={{ left: tip.x + 16, top: tip.y + 16 }}><b>{tip.text}</b></div>}
    </div>
  );
}
