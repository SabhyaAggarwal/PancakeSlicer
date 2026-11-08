export const MAGIC = 'ORCAMV01';
export const TRANSFER_MAGIC = 'ORCAMT02';
export const PLANES_MAGIC = 'ORCAMT01';
export const TRANSFER_FORM = 'mt2';

export const CHUNK = 16384;
export const transferBlockSize = (k: number) => CHUNK >> Math.max(0, 4 - k);
export const BLOCK = 16384;

export const MoveType = {
  Noop: 0, Retract: 1, Unretract: 2, Seam: 3, ToolChange: 4, ColorChange: 5, PausePrint: 6, CustomGCode: 7,
  Travel: 8, Wipe: 9, Extrude: 10,
} as const;

export const ROLE_NAMES = [
  'None', 'Inner wall', 'Outer wall', 'Overhang wall', 'Sparse infill', 'Internal solid infill', 'Top surface',
  'Bottom surface', 'Ironing', 'Bridge', 'Internal bridge', 'Gap infill', 'Skirt', 'Brim', 'Support',
  'Support interface', 'Support transition', 'Prime tower', 'Custom', 'Multiple',
];
export const ROLE_COUNT = ROLE_NAMES.length;

export interface PrintStatistics {
  total_used_filament_mm: number;
  total_extruded_volume_mm3: number;
  total_weight_g: number;
  total_cost: number;
  total_wipe_tower_filament_mm: number;
  total_toolchanges: number;
  per_extruder: { extruder_id: number; used_filament_mm: number; used_filament_g: number }[];
}

export function readStatistics(value: unknown): PrintStatistics | undefined {
  if (!value || typeof value !== 'object') return undefined;
  const s = value as Record<string, unknown>;
  const nonnegative = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v >= 0;
  if (!['total_used_filament_mm', 'total_extruded_volume_mm3', 'total_weight_g', 'total_cost',
    'total_wipe_tower_filament_mm', 'total_toolchanges'].every((k) => nonnegative(s[k])) ||
    !Number.isSafeInteger(s.total_toolchanges) || !Array.isArray(s.per_extruder)) return undefined;
  const ids = new Set<number>();
  for (const e of s.per_extruder) {
    if (!e || !Number.isInteger(e.extruder_id) || e.extruder_id < 0 || e.extruder_id > 255 ||
      ids.has(e.extruder_id) || !nonnegative(e.used_filament_mm) || !nonnegative(e.used_filament_g)) return undefined;
    ids.add(e.extruder_id);
  }
  return value as PrintStatistics;
}

export interface StreamMeta {
  statistics?: PrintStatistics;
  extruder_colors?: string[];
  color_print_colors?: string[];
  filament_names?: string[];
  estimated_times?: { time: number | null; prepare_time: number | null }[];
  belt_tilt_angle?: number | null;
  machine_frame_transform_active?: boolean;
  [k: string]: unknown;
}

export interface Header {
  moveCount: number;
  stateCount: number;
  layerCount: number;
  headerSize: number;
  moveStride: number;
  stateStride: number;
  layerStride: number;
  metaBytes: number;
  block: number;
  transfer: 0 | 1 | 2;
  extended: boolean;
  oriented?: boolean;
  tableRotary?: boolean;
  tableAC?: boolean;
}

export interface States {
  fan: Float32Array;
  temp: Float32Array;
  pa: Float32Array;
  accel: Float32Array;
  jerk: Float32Array;
  printZ: Float32Array;
  label: Int32Array;
  shell: Uint8Array;
  band: Int8Array;
  patch: Int16Array;
}

export interface MoveChunk {
  start: number;
  count: number;
  pos: Float32Array;
  type: Uint8Array;
  role: Uint8Array;
  extruder: Uint8Array;
  cp: Uint8Array;
  de: Float32Array;
  orientation: Float32Array;
  inverseTime: Float32Array;
  actual: Float32Array;
  clearance: Float32Array;
  feed: Float32Array;
  width: Float32Array;
  height: Float32Array;
  mm3: Float32Array;
  time: Float32Array;
  state: Uint32Array;
  gcode: Uint32Array;
  layer: Uint32Array;
}

export interface LayerRec {
  id: number;
  first: number;
  z: number;
}

export function readHeader(dv: DataView): Header {
  let magic = '';
  for (let i = 0; i < 8; ++i) magic += String.fromCharCode(dv.getUint8(i));
  if (magic !== MAGIC && magic !== TRANSFER_MAGIC && magic !== PLANES_MAGIC) throw new Error(`not a move stream (magic ${JSON.stringify(magic)})`);
  if (dv.getUint32(24, true) !== 1) throw new Error(`unknown units ${dv.getUint32(24, true)}`);
  const u64 = (o: number) => {
    const v = dv.getBigUint64(o, true);
    if (v > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error('count out of range');
    return Number(v);
  };
  const h: Header = {
    headerSize: dv.getUint32(8, true),
    moveStride: dv.getUint32(12, true),
    stateStride: dv.getUint32(16, true),
    layerStride: dv.getUint32(20, true),
    moveCount: u64(32),
    stateCount: dv.getUint32(40, true),
    layerCount: dv.getUint32(44, true),
    metaBytes: u64(48),
    block: magic === MAGIC ? 0 : u64(56),
    transfer: magic === TRANSFER_MAGIC ? 2 : magic === PLANES_MAGIC ? 1 : 0,
    extended: (dv.getUint32(28, true) & 1) !== 0,
    oriented: (dv.getUint32(28, true) & 0x10000) !== 0,
    tableRotary: (dv.getUint32(28, true) & 0x20000) !== 0,
    tableAC: (dv.getUint32(28, true) & 0x40000) !== 0,
  };
  if (h.headerSize < 64 || h.moveStride < (h.extended ? 60 : 52) + (h.oriented ? 16 : 0) || h.stateStride < 32 || h.layerStride < 16) throw new Error('bad strides');
  if (h.block && (h.block !== CHUNK || h.moveStride % 4)) throw new Error(`unknown transfer blocks (${h.block} moves, stride ${h.moveStride})`);
  return h;
}

export function readStates(dv: DataView, offset: number, h: Header): States {
  const n = h.stateCount;
  const s: States = {
    fan: new Float32Array(n), temp: new Float32Array(n), pa: new Float32Array(n), accel: new Float32Array(n),
    jerk: new Float32Array(n), printZ: new Float32Array(n), label: new Int32Array(n), shell: new Uint8Array(n),
    band: new Int8Array(n), patch: new Int16Array(n),
  };
  for (let i = 0, o = offset; i < n; ++i, o += h.stateStride) {
    s.fan[i] = dv.getFloat32(o, true);
    s.temp[i] = dv.getFloat32(o + 4, true);
    s.pa[i] = dv.getFloat32(o + 8, true);
    s.accel[i] = dv.getFloat32(o + 12, true);
    s.jerk[i] = dv.getFloat32(o + 16, true);
    s.printZ[i] = dv.getFloat32(o + 20, true);
    s.label[i] = dv.getInt32(o + 24, true);
    s.shell[i] = dv.getUint8(o + 28);
    s.band[i] = dv.getInt8(o + 29);
    s.patch[i] = dv.getInt16(o + 30, true);
  }
  return s;
}

export const isOption = (t: number) => t >= MoveType.Retract && t <= MoveType.CustomGCode;

export function shortTime(t: number): string {
  let d = Math.floor(t / 86400), h = Math.floor((t % 86400) / 3600), m = Math.floor((t % 3600) / 60);
  const sec = Math.floor(t % 60);
  if (d + h > 0 && sec >= 30 && ++m === 60) {
    m = 0;
    if (++h === 24) (h = 0), ++d;
  }
  return d > 0 ? `${d}d${h}h${m}m` : h > 0 ? `${h}h${m}m` : m > 0 ? `${m}m${sec}s` : t >= 1 ? `${sec}s` : t > 0 ? '<1s' : '0s';
}
export const spacedTime = (t: number) => shortTime(t).replace(/(\d)([a-z]+)(?=\d)/g, '$1$2 ');
