import { useState } from 'react';
import { effectiveConfig, getState } from '../state/store';
import {
  NONPLANAR_COLORS, NONPLANAR_HUES, NONPLANAR_PLANAR_COLOR, OPTION_COLORS, RANGE_PALETTE, ROLE_COLORS, VIEW_TYPES, hexToRgb,
  nonplanarIndexColor, type ColorRange, type RGB, type Ranges, type ViewType,
} from '../preview/colors';
import type { PreviewData } from '../preview/data';
import { ROLE_NAMES, shortTime } from '../preview/format';
import type { PreviewScene } from '../preview/PreviewScene';
import type { PreviewUi } from './PreviewView';

const css = (c: RGB) => `rgb(${c[0]},${c[1]},${c[2]})`;

const SHOW_STATS = new URLSearchParams(location.search).has('stats');

export function formatTime(s: number): string {
  if (!Number.isFinite(s)) return '-';
  s = Math.round(s);
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return [d && `${d}d`, (d || h) && `${h}h`, (d || h || m) && `${m}m`, `${sec}s`].filter(Boolean).join(' ');
}

function Swatch({ c }: { c: RGB }) {
  return <span className="lg-swatch" style={{ background: css(c) }} />;
}

function RangeRows({ range, decimals, title }: { range: ColorRange | undefined; decimals: number; title?: string }) {
  if (!range) return null;
  const values = range.values();
  const rows = values.length === 2
    ? [[values[1], RANGE_PALETTE.length - 1], [values[0], 0]]
    : values.map((v, i) => [v, i] as [number, number]).reverse();
  return (
    <>
      {title && <div className="lg-sub">{title}</div>}
      {rows.map(([v, i], k) => (
        <div className="lg-row" key={k}>
          <Swatch c={RANGE_PALETTE[i]} />
          <span>{v.toFixed(decimals)}</span>
        </div>
      ))}
    </>
  );
}

function filamentInfo(d: PreviewData) {
  const s = getState();
  return d.filamentMm.map((mm, i) => {
    const cfg = effectiveConfig(s, 'filament', Math.min(i, Math.max(0, s.filamentSlots.length - 1)));
    const num = (v: unknown, def: number) => {
      const x = Number(Array.isArray(v) ? v[0] : v);
      return Number.isFinite(x) && x > 0 ? x : def;
    };
    const dia = num(cfg.filament_diameter, 1.75);
    const density = num(cfg.filament_density, 1.24);
    const grams = ((mm ?? 0) * Math.PI * (dia / 2) ** 2 * density) / 1000;
    return { mm: mm ?? 0, grams };
  });
}

export function Legend(props: {
  data: PreviewData;
  ui: PreviewUi;
  setUi(f: (u: PreviewUi) => PreviewUi): void;
  ranges: Ranges | null;
  range: [number, number];
  scene: PreviewScene | null;
}) {
  const { data: d, ui, setUi, ranges } = props;
  const [open, setOpen] = useState(true);
  const [mode, setMode] = useState(0);
  const modes = (d.meta.estimated_times ?? []).filter((m) => typeof m?.time === 'number' && m.time > 0).length;
  const total = d.totalTime(mode);
  const prepare = d.meta.estimated_times?.[mode]?.prepare_time;
  const fil = filamentInfo(d);
  const extrudeTime = d.roleTime.reduce((a, b) => a + b, 0);
  const toggle = (k: keyof PreviewUi['vis']) => setUi((u) => ({ ...u, vis: { ...u.vis, [k]: !u.vis[k] } }));
  const toggleRole = (r: number) =>
    setUi((u) => {
      const h = new Set(u.vis.hiddenRoles);
      if (h.has(r)) h.delete(r);
      else h.add(r);
      return { ...u, vis: { ...u.vis, hiddenRoles: h } };
    });

  const body = () => {
    switch (ui.view) {
      case 'feature':
        return (
          <table className="lg-table">
            <thead>
              <tr><th>Line Type</th><th>Time</th><th>%</th><th>Usage</th></tr>
            </thead>
            <tbody>
              {ROLE_NAMES.map((name, r) => (d.roleTime[r] > 0 || d.roleFilament[r] > 0) && (
                <tr key={r} className={ui.vis.hiddenRoles.has(r) ? 'off' : ''} onClick={() => toggleRole(r)} title="Click to show or hide">
                  <td><Swatch c={ROLE_COLORS[r]} />{name}</td>
                  <td>{formatTime(d.roleTime[r])}</td>
                  <td>{extrudeTime > 0 ? `${((100 * d.roleTime[r]) / extrudeTime).toFixed(1)}%` : ''}</td>
                  <td>{(d.roleFilament[r] / 1000).toFixed(2)} m</td>
                </tr>
              ))}
            </tbody>
          </table>
        );
      case 'height': return <RangeRows range={ranges?.height} decimals={2} />;
      case 'width': return <RangeRows range={ranges?.width} decimals={2} />;
      case 'speed': return <RangeRows range={ranges?.speed} decimals={0} title="Speed (mm/s)" />;
      case 'acceleration': return <RangeRows range={ranges?.acceleration} decimals={0} title="Acceleration (mm/s²)" />;
      case 'jerk': return <RangeRows range={ranges?.jerk} decimals={0} title="Jerk (mm/s)" />;
      case 'flow': return <RangeRows range={ranges?.flow} decimals={2} title="Flow (mm³/s)" />;
      case 'fan': return <RangeRows range={ranges?.fan} decimals={0} title="Fan Speed (%)" />;
      case 'temperature': return <RangeRows range={ranges?.temperature} decimals={0} title="Temperature (°C)" />;
      case 'pressure_advance': return <RangeRows range={ranges?.pa} decimals={3} />;
      case 'layer_time': return <RangeRows range={ranges?.layerTime} decimals={0} title="Layer Time (s)" />;
      case 'layer_time_log': return <RangeRows range={ranges?.layerTimeLog} decimals={0} title="Layer Time (s)" />;
      case 'filament': {
        const s = getState();
        const cols = (d.meta.extruder_colors?.length ? d.meta.extruder_colors : []).map(hexToRgb);
        const names = d.meta.filament_names?.length ? d.meta.filament_names : s.filamentSlots;
        return (
          <table className="lg-table">
            <thead><tr><th>Filament</th><th>Total</th></tr></thead>
            <tbody>
              {fil.map((f, i) => (
                <tr key={i}>
                  <td><Swatch c={cols[i] ?? hexToRgb('#F2754E')} />{i + 1} {names[i] ?? ''}</td>
                  <td>{(f.mm / 1000).toFixed(2)} m · {f.grams.toFixed(2)} g</td>
                </tr>
              ))}
            </tbody>
          </table>
        );
      }
      case 'nonplanar': return <NonplanarKey d={d} ui={ui} setUi={setUi} ranges={ranges} />;
    }
  };

  const options: [keyof PreviewUi['vis'], string, RGB][] = [
    ['travel', 'Travel', OPTION_COLORS.travel],
    ['wipe', 'Wipe', OPTION_COLORS.wipe],
    ['retract', 'Retract', OPTION_COLORS.retract],
    ['unretract', 'Unretract', OPTION_COLORS.unretract],
    ['seam', 'Seams', OPTION_COLORS.seam],
  ];
  const stats = props.scene?.lastFrame;
  return (
    <div className="legend">
      <div className="lg-head">
        <select value={ui.view} onChange={(e) => setUi((u) => ({ ...u, view: e.target.value as ViewType }))}>
          {VIEW_TYPES.map((v) => <option key={v.key} value={v.key}>{v.label}</option>)}
        </select>
        <button className="lg-collapse" onClick={() => setOpen(!open)} title={open ? 'Collapse' : 'Expand'}>{open ? '▴' : '▾'}</button>
      </div>
      {open && (
        <>
          <div className="lg-body">{body()}</div>
          <div className="lg-sub">Options</div>
          <div className="lg-options">
            {options.map(([k, label, c]) => (
              <label key={k} className="lg-opt">
                <input type="checkbox" checked={ui.vis[k] as boolean} onChange={() => toggle(k)} />
                <Swatch c={c} />{label}
                {k === 'travel' && d.travelTime > 0 && <span className="lg-time">{formatTime(d.travelTime)}</span>}
              </label>
            ))}
            <label className="lg-opt">
              <input type="checkbox" checked={ui.shells} onChange={() => setUi((u) => ({ ...u, shells: !u.shells }))} />
              Shells
            </label>
          </div>
          <div className="lg-sub">
            {ui.view === 'feature' ? 'Total estimation' : 'Time Estimation'}
            {modes > 1 && mode === 0 ? ' [Normal mode]' : ''}
          </div>
          <table className="lg-summary">
            <tbody>
              {ui.view === 'feature' && (
                <tr><td>Total Filament:</td><td>{(fil.reduce((a, f) => a + f.mm, 0) / 1000).toFixed(2)} m  {fil.reduce((a, f) => a + f.grams, 0).toFixed(2)} g</td></tr>
              )}
              {prepare ? <tr><td>Prepare time:</td><td>{shortTime(prepare)}</td></tr> : null}
              <tr><td>Model printing time:</td><td>{shortTime(total - (prepare ?? 0))}</td></tr>
              <tr><td>Total time:</td><td>{shortTime(total)}</td></tr>
            </tbody>
          </table>
          {modes > 1 && (
            <button className="lg-mode" onClick={() => setMode(1 - mode)}>{mode ? 'Show normal mode' : 'Show stealth mode'}</button>
          )}
          {stats && SHOW_STATS && (
            <div className="lg-perf" title="Last frame: blocks drawn / culled / at level of detail, segments drawn, frame CPU and GPU time">
              {stats.blocks} blocks ({stats.lodBlocks} LOD, {stats.culled} culled) · {(stats.segments / 1e6).toFixed(2)} M seg · {stats.frameMs.toFixed(1)} ms
              {Number.isFinite(stats.gpuMs) ? ` · GPU ${stats.gpuMs.toFixed(1)} ms` : ''}
              {d.done ? ` · loaded in ${(d.loadMs / 1000).toFixed(2)} s` : ''}
            </div>
          )}
        </>
      )}
    </div>
  );
}

function NonplanarKey({ d, ui, setUi, ranges }: { d: PreviewData; ui: PreviewUi; setUi(f: (u: PreviewUi) => PreviewUi): void; ranges: Ranges | null }) {
  const st = d.states!;
  const shells = new Set<number>(), bands = new Set<number>(), patches = new Set<number>();
  for (let i = 0; i < st.shell.length; ++i)
    if (st.shell[i] !== 0xff) {
      shells.add(st.shell[i]);
      bands.add(st.band[i]);
      patches.add(st.patch[i]);
    }
  const shellCount = shells.size ? Math.max(...shells) + 1 : 0;
  const indexRows = (values: number[], word: string, plural: string, unlisted: string) => {
    const sorted = [...values].sort((a, b) => a - b);
    const repeat = sorted.length > 0 && sorted[sorted.length - 1] >= NONPLANAR_HUES;
    const rows: { c: RGB; label: string }[] = [];
    if (sorted[0] < 0) rows.push({ c: nonplanarIndexColor(-1), label: unlisted });
    for (let hue = repeat ? NONPLANAR_HUES - 1 : sorted[sorted.length - 1]; hue >= 0; --hue) {
      const marked = sorted.filter((b) => b >= 0 && (repeat ? b % NONPLANAR_HUES === hue : b === hue));
      if (!marked.length) continue;
      const label = marked.slice(0, 3).map((b) => b + 1).join(', ') + (marked.length > 3 ? ', …' : '');
      rows.push({ c: nonplanarIndexColor(hue), label: `${marked.length > 1 ? plural : word} ${label}` });
    }
    return rows;
  };
  let rows: { c: RGB; label: string }[] = [];
  if (ui.nonplanar === 'shell') {
    for (let k = shellCount - 1; k >= 0; --k) rows.push({ c: ranges ? ranges.shell.colorAt(k) : [0, 0, 0], label: `Shell ${k + 1}` });
  } else if (ui.nonplanar === 'band') rows = indexRows([...bands], 'Band', 'Bands', 'Unbanded');
  else if (ui.nonplanar === 'patch') rows = indexRows([...patches], 'Patch', 'Patches', 'No patch');
  return (
    <div>
      <div className="lg-radios">
        {NONPLANAR_COLORS.map((c) => (
          <label key={c.key}>
            <input type="radio" checked={ui.nonplanar === c.key} onChange={() => setUi((u) => ({ ...u, nonplanar: c.key }))} />
            {c.label}
          </label>
        ))}
      </div>
      {shellCount === 0 && <div className="lg-sub">No curved passes</div>}
      {shellCount > 0 && rows.map((r, i) => (
        <div className="lg-row" key={i}><Swatch c={r.c} /><span>{r.label}</span></div>
      ))}
      {shellCount > 0 && ui.nonplanar === 'bead_height' && <RangeRows range={ranges?.beadHeight} decimals={2} title="Bead height (mm)" />}
      {shellCount > 0 && ui.nonplanar === 'slope' && <RangeRows range={ranges?.slope} decimals={0} title="Surface slope (°)" />}
      <div className="lg-row"><Swatch c={NONPLANAR_PLANAR_COLOR} /><span>Planar</span></div>
    </div>
  );
}
