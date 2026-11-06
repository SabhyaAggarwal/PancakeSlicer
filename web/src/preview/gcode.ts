







const ROLES: Record<string, number> = {
  'Inner wall': 1, 'Outer wall': 2, 'Overhang wall': 3, 'Sparse infill': 4, 'Internal solid infill': 5, 'Top surface': 6,
  'Bottom surface': 7, Ironing: 8, Bridge: 9, 'Internal Bridge': 10, 'Gap infill': 11, Skirt: 12, Brim: 13, Support: 14,
  'Support interface': 15, 'Support transition': 16, 'Prime tower': 17, Custom: 18, Multiple: 19,
  Perimeter: 1, 'External perimeter': 2, 'Overhang perimeter': 3, 'Internal infill': 4, 'Solid infill': 5,
  'Top solid infill': 6, 'Bridge infill': 9, 'Gap fill': 11, 'Skirt/Brim': 12, 'Support material': 14,
  'Support material interface': 15, 'Wipe tower': 17,
};
const T = { Noop: 0, Retract: 1, Unretract: 2, ToolChange: 4, ColorChange: 5, Pause: 6, Travel: 8, Wipe: 9, Extrude: 10 } as const;
const STATE = 32, LAYER = 16;


function duration(text: string): number | null {
  let s = 0, any = false;
  for (const [, v, u] of text.matchAll(/(\d+(?:\.\d+)?)\s*([dhms])/g)) { s += Number(v) * { d: 86400, h: 3600, m: 60, s: 1 }[u as 'd']; any = true; }
  return any ? s : null;
}

function* linesOf(text: string): Generator<string> {
  let start = 0;
  while (start < text.length) {
    const end = text.indexOf('\n', start);
    const stop = end < 0 ? text.length : end;
    yield text.slice(start, text[stop - 1] === '\r' ? stop - 1 : stop);
    if (end < 0) break;
    start = end + 1;
  }
}

export function gcodeToStream(text: string): ArrayBuffer {
  const config = new Map<string, string>();
  for (const l of linesOf(text)) {
    const m = /^;\s*([a-z_0-9]+)\s*=\s*(.*)$/.exec(l);
    if (m && !config.has(m[1])) config.set(m[1], m[2].trim());
  }
  const layout = config.get('nonplanar_kinematics') ?? 'none';
  const rotary = /^(table_bc|table_ac|head_ab|head_bc)$/.test(layout);
  const oriented = rotary || /^[ \t]*(?:N\d+\s*)?G0?[0-3][^;\r\n]*[ABC]\s*[+-]?[\d.]/mi.test(text);
  const MOVE = oriented ? 68 : 52, angles = [0, 0, 0];
  const vector = (key: string, count: number) => Array.from({ length: count }, (_, i) => Number((config.get(key) ?? '').split(',')[i]) || 0);
  const home = vector('rotary_axis_home', 2), reversed = vector('rotary_axis_reversed', 2);
  const pivot = vector('rotary_pivot', 3), mount = vector('rotary_tool_mount', 3), tableMoves = vector('rotary_linear_moves_table', 3);
  const toolLength = Number(config.get('rotary_tool_length')) || 0, tableHeight = Number(config.get('rotary_table_height')) || 0;
  const toTip = (x: number, y: number, z: number, pose: number[]): [number, number, number] => {
    if (!rotary || config.get('rotary_firmware_rtcp') === '1') return [x, y, z];
    const letters = layout.endsWith('ab') ? 'AB' : layout.endsWith('ac') ? 'AC' : 'BC';
    const u = (pose['ABC'.indexOf(letters[0])] - home[0]) * (reversed[0] ? -1 : 1) * Math.PI / 180;
    const v = (pose['ABC'.indexOf(letters[1])] - home[1]) * (reversed[1] ? -1 : 1) * Math.PI / 180;
    const su = Math.sin(u), cu = Math.cos(u), sv = Math.sin(v), cv = Math.cos(v);
    if (layout.startsWith('table')) {
      const r = [x, y, z].map((q, i) => (tableMoves[i] ? -q : q) - pivot[i] + mount[i] - (i === 2 ? toolLength : 0));

      const qx = layout === 'table_bc' ? cu * r[0] - su * r[2] : r[0];
      const qy = layout === 'table_bc' ? r[1] : cu * r[1] + su * r[2];
      const qz = layout === 'table_bc' ? su * r[0] + cu * r[2] : -su * r[1] + cu * r[2];
      return [cv * qx + sv * qy, -sv * qx + cv * qy, qz - tableHeight];
    }
    const axis = layout === 'head_ab' ? [sv * cu, -su, cv * cu] : [su * cv, su * sv, cu];
    return [x - toolLength * axis[0], y - toolLength * axis[1], z - toolLength * (axis[2] - 1)];
  };
  const diameter = Number((config.get('filament_diameter') ?? '1.75').split(/[,;]/)[0]) || 1.75;
  const area = Math.PI * (diameter / 2) ** 2;


  const PAGE = 16384;
  const pages: Uint8Array[] = [];
  let moveCount = 0, previousZ = 0;
  let previousTip = [0, 0, 0];
  let page: DataView;
  const layers: [number, number, number][] = [];
  const best: [number, number, number][] = [];
  const states: number[][] = [], stateKey = new Map<string, number>();
  const labels = new Map<string, number>();
  let x = 0, y = 0, z = 0, e = 0, f = 0, relXYZ = false, relE = false, inverse = false;
  let role = 0, width = 0, height = 0, layer = 0, extruder = 0, cp = 0, wipe = false;
  let fan = 0, temp = 0, printZ = 0, label = -1, shell = 255, band = -1, patch = -1;
  const stateOf = () => {
    const s = [fan, temp, 0, 0, 0, printZ, label, shell, band, patch];
    const k = s.join(',');
    let i = stateKey.get(k);
    if (i === undefined) { i = states.length; states.push(s); stateKey.set(k, i); }
    return i;
  };
  const push = (line: number, type: number, nx: number, ny: number, nz: number, de: number, len: number, seconds?: number, pose = angles) => {
    const tip = moveCount ? toTip(nx, ny, nz, pose) : [0, 0, 0];
    const distance = rotary && moveCount ? Math.hypot(tip[0] - previousTip[0], tip[1] - previousTip[1], tip[2] - previousTip[2]) : len;
    const mm3 = type === T.Extrude && distance > 0 ? (de * area) / distance : 0;
    const h = height || (type === T.Extrude ? Math.max(0.05, tip[2] - previousZ) : 0);
    const w = width || (type === T.Extrude && h > 0 ? mm3 / h : 0);

    const layerId = Math.max(1, layer) - 1, state = stateOf();
    if (moveCount > 0) {
      if (layers.at(-1)?.[0] !== layerId) { layers.push([layerId, moveCount, 0]); best.push([0, 0, 0]); }
      const b = best.at(-1)!;
      b[0] = Math.max(b[0], printZ);
      if (type === T.Extrude && role !== 18) { b[2] = Math.max(b[2], tip[2]); if (shell === 255) b[1] = Math.max(b[1], tip[2]); }
    }
    if (moveCount % PAGE === 0) {
      const bytes = new Uint8Array(PAGE * MOVE);
      pages.push(bytes); page = new DataView(bytes.buffer);
    }
    const o = (moveCount % PAGE) * MOVE;
    page.setUint32(o, line, true); page.setUint8(o + 4, type); page.setUint8(o + 5, role); page.setUint8(o + 6, extruder); page.setUint8(o + 7, cp);
    const feed = seconds !== undefined && seconds > 0 ? distance / seconds : f / 60;
    const time = seconds ?? (f > 0 ? distance / (f / 60) : 0);
    const values = [...tip, de, feed, w, h, mm3, time];
    for (let k = 0; k < values.length; ++k) page.setFloat32(o + 8 + k * 4, values[k], true);
    if (oriented) {
      for (let k = 0; k < 3; ++k) page.setFloat32(o + 52 + k * 4, pose[k], true);
      page.setFloat32(o + 64, inverse ? f : 0, true);
    }
    page.setUint32(o + 44, layerId, true); page.setUint32(o + 48, state, true);
    moveCount++; previousZ = tip[2]; previousTip = tip;
  };
  push(0, T.Noop, 0, 0, 0, 0, 0);

  let line = 0;
  for (const raw of linesOf(text)) {
    line++;
    const semi = raw.indexOf(';');
    const comment = semi >= 0 ? raw.slice(semi + 1) : '';
    const code = (semi >= 0 ? raw.slice(0, semi) : raw).trim();
    if (comment) {
      const c = comment, ct = c.trim();
      let m: RegExpExecArray | null;
      if ((m = /^ ?FEATURE: (.*)$|^TYPE:(.*)$/.exec(c))) role = ROLES[(m[1] ?? m[2]).trim()] ?? role;
      else if ((m = /^ ?(?:LINE_WIDTH|WIDTH): ?([\d.]+)/.exec(c))) width = Number(m[1]);
      else if ((m = /^ ?(?:LAYER_HEIGHT|HEIGHT): ?([\d.]+)/.exec(c))) height = Number(m[1]);
      else if (ct === 'CHANGE_LAYER' || ct === 'LAYER_CHANGE') layer++;

      else if ((m = /^ Z_HEIGHT: ?([\d.]+)/.exec(c))) printZ = Number(m[1]);
      else if (ct === 'WIPE_START') wipe = true;
      else if (ct === 'WIPE_END') wipe = false;
      else if ((m = /^_NONPLANAR_BEGIN(.*)$/.exec(ct))) {
        const get = (k: string) => { const value = new RegExp(`${k}=(-?\\d+)`).exec(m![1])?.[1]; return value === undefined ? undefined : Number(value); };
        patch = Math.max(-1, Math.min(32767, get('patch') ?? -1)); shell = Math.max(0, Math.min(254, get('shell') ?? 0)); band = Math.max(-1, Math.min(127, get('band') ?? -1));
      } else if (ct.startsWith('_NONPLANAR_END')) { shell = 255; band = -1; patch = -1; }
      else if ((m = /^ ?start printing object, unique label id: ?(\d+)/.exec(c))) label = Number(m[1]);
      else if (/^ ?stop printing object/.test(c)) label = -1;
    }
    if (!code) continue;
    const command = /^(?:N\d+\s*)?([GMT]\d+|[A-Z_]+)/i.exec(code);
    if (!command) continue;
    const word = command[1].toUpperCase().replace(/^([GMT])0+(?=\d)/, '$1');
    const args: Record<string, number> = {};
    for (const m of code.slice(command[0].length).matchAll(/([A-Z])\s*([+-]?(?:\d+\.?\d*|\.\d+))/gi)) {
      const v = Number(m[2]);
      if (!Number.isFinite(v)) throw new Error(`Invalid number at G-code line ${line}`);
      args[m[1].toUpperCase()] = v;
    }
    const arg = (k: string) => args[k];
    if (word === 'G90') { relXYZ = false; relE = false; continue; }
    if (word === 'G91') { relXYZ = true; relE = true; continue; }
    if (word === 'G93') { inverse = true; continue; }
    if (word === 'G94') { inverse = false; continue; }
    if (word === 'M82') { relE = false; continue; }
    if (word === 'M83') { relE = true; continue; }
    if (word === 'G92') { const v = arg('E'); if (v !== undefined) e = v; continue; }
    if (word === 'M106') { fan = Math.min(100, ((arg('S') ?? 255) / 255) * 100); continue; }
    if (word === 'M107') { fan = 0; continue; }
    if (word === 'M104' || word === 'M109') { const s = arg('S'); if (s !== undefined) temp = s; continue; }
    if (word === 'M486') { const s = arg('S'); if (s !== undefined) label = s < 0 ? -1 : s; continue; }
    if (word === 'EXCLUDE_OBJECT_START') {
      const name = /NAME=(\S+)/i.exec(code)?.[1] ?? '';
      if (!labels.has(name)) labels.set(name, labels.size);
      label = labels.get(name)!;
      continue;
    }
    if (word === 'EXCLUDE_OBJECT_END') { label = -1; continue; }
    if (/^T\d+$/.test(word)) {
      const t = Number(word.slice(1));
      if (t < 16 && t !== extruder) { extruder = t; cp = t; push(line, T.ToolChange, x, y, z, 0, 0); }
      continue;
    }
    if (word === 'M600') { cp++; push(line, T.ColorChange, x, y, z, 0, 0); continue; }
    if (word === 'M601' || word === 'M400' && arg('U') === 1) { push(line, T.Pause, x, y, z, 0, 0); continue; }
    if (word !== 'G0' && word !== 'G1' && word !== 'G2' && word !== 'G3') continue;
    const fx = arg('F'); if (fx !== undefined) f = fx;
    const previousAngles = angles.slice();
    for (let k = 0; k < 3; ++k) {
      const a = arg('ABC'[k]);
      if (a !== undefined) angles[k] = relXYZ ? angles[k] + a : a;
    }
    const rotated = angles.some((a, k) => a !== previousAngles[k]);
    const ax = arg('X'), ay = arg('Y'), az = arg('Z'), ae = arg('E');
    const nx = ax === undefined ? x : relXYZ ? x + ax : ax;
    const ny = ay === undefined ? y : relXYZ ? y + ay : ay;
    const nz = az === undefined ? z : relXYZ ? z + az : az;
    const de = ae === undefined ? 0 : relE ? ae : ae - e;
    if (ae !== undefined) e = relE ? e + ae : ae;
    const arc = word === 'G2' || word === 'G3';
    const moved = nx !== x || ny !== y || nz !== z || rotated || arc && ((arg('I') ?? 0) !== 0 || (arg('J') ?? 0) !== 0);
    const seconds = inverse && word !== 'G0' && f > 0 ? 60 / f : undefined;
    const type = !moved ? (de < 0 ? T.Retract : de > 0 ? T.Unretract : -1) : wipe ? T.Wipe : de > 0 ? T.Extrude : T.Travel;
    if (type < 0) continue;
    if ((word === 'G2' || word === 'G3') && moved) {

      const cx = x + (arg('I') ?? 0), cy = y + (arg('J') ?? 0), r = Math.hypot(x - cx, y - cy);
      let a0 = Math.atan2(y - cy, x - cx), a1 = Math.atan2(ny - cy, nx - cx);
      if (word === 'G2' && a1 >= a0) a1 -= 2 * Math.PI;
      if (word === 'G3' && a1 <= a0) a1 += 2 * Math.PI;
      const sweep = a1 - a0, k = Math.max(1, Math.ceil(Math.max(Math.abs(sweep) / (Math.PI / 36), (Math.abs(sweep) * r) / 1)));
      let px = x, py = y, pz = z;
      for (let i = 1; i <= k; i++) {
        const a = a0 + (sweep * i) / k;
        const qx = i === k ? nx : cx + r * Math.cos(a), qy = i === k ? ny : cy + r * Math.sin(a), qz = z + ((nz - z) * i) / k;
        push(line, type, qx, qy, qz, de / k, Math.hypot(qx - px, qy - py, qz - pz), seconds === undefined ? undefined : seconds / k, angles.map((a, j) => previousAngles[j] + (a - previousAngles[j]) * i / k));
        px = qx; py = qy; pz = qz;
      }
    } else push(line, type, nx, ny, nz, de, Math.hypot(nx - x, ny - y, nz - z), seconds);
    x = nx; y = ny; z = nz;
  }


  layers.forEach((l, k) => { l[2] = best[k][0] || best[k][1] || best[k][2]; });

  const colours = (config.get('filament_colour') ?? config.get('extruder_colour') ?? '').split(/[;,]/).map((c) => c.trim().replace(/"/g, '')).filter((c) => /^#[0-9a-f]{6}$/i.test(c));
  const total = duration(/total estimated time: ([^;]+)/.exec(text)?.[1] ?? /estimated printing time \(normal mode\) = (.*)/.exec(text)?.[1] ?? '');
  const meta = new TextEncoder().encode(JSON.stringify({
    extruder_colors: colours.length ? colours : ['#00AE42'],
    color_print_colors: colours.length ? colours : ['#00AE42'],
    filament_names: (config.get('filament_settings_id') ?? '').split(';').map((s) => s.trim().replace(/^"|"$/g, '')).filter(Boolean),
    estimated_times: total === null ? [] : [{ time: total, prepare_time: 0 }],
    belt_tilt_angle: 0, belt_z_origin: 0, machine_frame_transform_active: false,
  }));

  const size = 64 + states.length * STATE + moveCount * MOVE + layers.length * LAYER + meta.length;
  const buf = new ArrayBuffer(size), dv = new DataView(buf);
  'ORCAMV01'.split('').forEach((ch, i) => dv.setUint8(i, ch.charCodeAt(0)));
  dv.setUint32(8, 64, true); dv.setUint32(12, MOVE, true); dv.setUint32(16, STATE, true); dv.setUint32(20, LAYER, true);
  dv.setUint32(24, 1, true); dv.setUint32(28, oriented ? 0x10000 | (layout.startsWith('table') ? 0x20000 | (layout === 'table_ac' ? 0x40000 : 0) : 0) : 0, true); dv.setBigUint64(32, BigInt(moveCount), true);
  dv.setUint32(40, states.length, true); dv.setUint32(44, layers.length, true); dv.setBigUint64(48, BigInt(meta.length), true);
  let o = 64;
  for (const s of states) {
    for (let k = 0; k < 6; k++) dv.setFloat32(o + 4 * k, s[k], true);
    dv.setInt32(o + 24, s[6], true); dv.setUint8(o + 28, s[7]); dv.setInt8(o + 29, s[8]); dv.setInt16(o + 30, s[9], true);
    o += STATE;
  }
  let left = moveCount * MOVE;
  for (const page of pages) {
    const n = Math.min(left, page.length);
    new Uint8Array(buf, o, n).set(page.subarray(0, n)); o += n; left -= n;
  }
  for (const [id, first, zl] of layers) { dv.setUint32(o, id, true); dv.setBigUint64(o + 4, BigInt(first), true); dv.setFloat32(o + 12, zl, true); o += LAYER; }
  new Uint8Array(buf, o).set(meta);
  return buf;
}
