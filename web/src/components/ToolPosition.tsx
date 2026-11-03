import { useMemo, useState } from 'react';
import type { PreviewData } from '../preview/data';
import type { ViewType } from '../preview/colors';
import { MoveType, ROLE_NAMES } from '../preview/format';
import { Caret } from './Caret';

const TYPE_NAMES = ['Noop', 'Retract', 'Unretract', 'Seam', 'Tool change', 'Color change', 'Pause print', 'Custom G-code', 'Travel', 'Wipe', 'Extrude'];
const NA = 'N/A';
let shownDefault = false;

function dhms(t: number) {
  const s = Math.round(t), d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d}d ${h}h ${m}m ${s % 60}s` : h ? `${h}h ${m}m ${s % 60}s` : m ? `${m}m ${s % 60}s` : `${s % 60}s`;
}

export default function ToolPosition({ data, at, view }: { data: PreviewData; at: number; view: ViewType }) {
  const [shown, setShown] = useState(shownDefault);
  const elapsed = useMemo(() => {
    const e = new Float64Array(data.loaded);
    for (let i = 1; i < data.loaded; ++i) e[i] = e[i - 1] + data.time[i];
    return e;
  }, [data, data.loaded]);
  let i = at;
  if (view !== 'feature' && (data.type[i] & 0x7f) === MoveType.Seam && i > 1) i -= 1;
  const type = data.type[i] & 0x7f, st = data.states!, s = data.state[i];
  const extrusion = type === MoveType.Extrude;
  const p = [data.pos[i * 4], data.pos[i * 4 + 1], data.pos[i * 4 + 2]];
  const max = Math.round(Math.max(...p));
  const digits = max > 9999 ? 1 : max > 999 ? 2 : 3;
  const flow = data.mm3[i] * data.feed[i];
  const ifExtrusion = (v: number, unit: string, f = 2) => (extrusion ? `${v.toFixed(f)}${unit}` : NA);
  const detail = (() => {
    switch (view) {
      case 'feature': return extrusion ? ROLE_NAMES[data.role[i]] : type !== MoveType.Noop ? TYPE_NAMES[type] : NA;
      case 'height': return `Height: ${ifExtrusion(data.height[i], '')}`;
      case 'width': return `Width: ${ifExtrusion(data.width[i], '')}`;
      case 'flow': return `Flow: ${ifExtrusion(flow, '')}`;
      case 'fan': return `Fan: ${st.fan[s].toFixed(0)}`;
      case 'temperature': return `Temperature: ${st.temp[s].toFixed(0)}`;
      case 'layer_time': case 'layer_time_log': return `Layer Time: ${(data.layerTimes[data.layer[i]] ?? 0).toFixed(1)}`;
      case 'filament': return `Tool: ${data.extruder[i] + 1}`;
      case 'acceleration': return `Acceleration: ${st.accel[s].toFixed(0)}`;
      case 'jerk': return `Jerk: ${st.jerk[s].toFixed(1)}`;
      case 'pressure_advance': return `PA: ${st.pa[s].toFixed(4)}`;
      case 'nonplanar': return extrusion ? (st.shell[s] !== 0xff ? `Patch ${st.patch[s]}, shell ${st.shell[s] + 1}` : 'Planar') : NA;
      case 'clearance': {
        const c = data.clearance[i];
        return c === undefined || Number.isNaN(c) ? 'Not checked' : Number.isFinite(c) ? `Clearance: ${c.toFixed(3)} mm` : 'Nothing within reach';
      }
      default: return '';
    }
  })();
  let length = NA;
  if (i > 0 && (extrusion || type === MoveType.Travel || type === MoveType.Wipe)) {
    let a = i, b = i, sum = 0;
    while (a > 1 && data.gcode[a - 1] === data.gcode[i]) --a;
    while (b + 1 < data.loaded && data.gcode[b + 1] === data.gcode[i]) ++b;
    for (let k = Math.max(a, 1); k <= b; ++k) sum += Math.hypot(data.pos[k * 4] - data.pos[k * 4 - 4], data.pos[k * 4 + 1] - data.pos[k * 4 - 3], data.pos[k * 4 + 2] - data.pos[k * 4 - 2]);
    length = `${sum.toFixed(3)} mm`;
  }
  const rows: [string, string][] = shown ? [
    ['Type', TYPE_NAMES[type] ?? NA],
    ['Line Type', extrusion ? ROLE_NAMES[data.role[i]] : NA],
    ['Width', ifExtrusion(data.width[i], ' mm', 3)],
    ['Height', ifExtrusion(data.height[i], ' mm', 3)],
    ['Length', length],
    ['Layer', String(data.layer[i] + 1)],
    ['Speed', `${data.feed[i].toFixed(1)} mm/s`],
    ['Acceleration', `${st.accel[s].toFixed(0)} mm/s²`],
    ['Jerk', `${st.jerk[s].toFixed(1)} mm/s`],
    ['Flow rate', ifExtrusion(flow, ' mm³/s', 3)],
    ['Fan speed', `${st.fan[s].toFixed(0)} %`],
    ['Temperature', `${st.temp[s].toFixed(0)} ℃`],
    ['Pressure Advance', st.pa[s].toFixed(4)],
    ['Time', `${dhms(elapsed[i] ?? 0)} (${data.time[i].toFixed(3)}s)`],
  ] : [];
  return (
    <div className="tool-position" aria-label="Tool position">
      {shown && (
        <table className="tool-properties">
          <tbody>{rows.map(([k, v]) => <tr key={k}><th>{k}</th><td>{v}</td></tr>)}</tbody>
        </table>
      )}
      <div className="tool-position-row">
        <button type="button" className="tool-fold" title={shown ? 'Hide properties' : 'Show properties'} aria-expanded={shown}
          onClick={() => { shownDefault = !shown; setShown(!shown); }}><Caret dir={shown ? 'down' : 'up'}/></button>
        <div>
          <div className="tool-axes">
            {(['x', 'y', 'z'] as const).map((a, k) => <span key={a}><span className={`axis ${a}`}>{a.toUpperCase()} </span>{p[k].toFixed(digits)}</span>)}
          </div>
          <div className="tool-detail"><span>Speed: {data.feed[i].toFixed(0)}</span>{detail && <span>{detail}</span>}</div>
        </div>
      </div>
    </div>
  );
}
