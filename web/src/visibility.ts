import { isHidden as permanentlyHidden, modeVisible, OPTIONS, splitKey, type TabDesc, type OptionMode, type OptionDef } from './options';
import type { Config, PresetType } from './api/types';
import { canOneShot } from './api/client';
import { effectiveMode, type State } from './state/store';
import { commonMode, hiddenReason } from './settings/common';

export function isHidden(k: string, s?: State, type: PresetType = 'filament', native?: OptionMode, more = false, slot?: number): boolean {
  const key = splitKey(k)[0];
  if (permanentlyHidden(key)) return true;
  if (!s) return false;
  if (s.settingsFocus?.type === type && s.settingsFocus.key === key) return false;
  const mode = effectiveMode(s), rank = native ?? OPTIONS[key]?.mode;
  if (!rank) return true;
  if (mode === 'develop' || mode === 'developer') return !modeVisible(rank, mode);
  const reason = hiddenReason(k, s, type, slot);
  if (reason && !(reason === 'other plate' && more)) return true;
  return !modeVisible(commonMode(type, key, s, rank), more && mode === 'simple' ? 'advanced' : mode);
}

export const settingVisible = (type: PresetType, key: string, s: State, native?: OptionMode, more = false, slot?: number): boolean =>
  !!OPTIONS[splitKey(key)[0]] && !isHidden(key, s, type, native, more, slot);

type Scalar = string | number | boolean | null | undefined;
export type Environment = Record<string, unknown>;
const ranks: Record<string, number> = { Simple: 0, Advanced: 1, Expert: 2, Develop: 3 };
const truth = (v: Scalar) => v === undefined ? undefined : !!v;

export function condition(expression: string, env: Environment): boolean | undefined {
  const tokens = expression.match(/"(?:[^"\\]|\\.)*"|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[A-Za-z_][\w.]*|&&|\|\||==|!=|>=|<=|[()!<>+*/-]/g) ?? [];
  if (tokens.join('') !== expression.replace(/\s+(?=(?:[^"\\]*"[^"\\]*")*[^"\\]*$)/g, '')) return undefined;
  let pos = 0;
  const precedence: Record<string, number> = { '||': 1, '&&': 2, '==': 3, '!=': 3, '<': 4, '>': 4, '<=': 4, '>=': 4, '+': 5, '-': 5, '*': 6, '/': 6 };
  function parse(min = 0): Scalar {
    const token = tokens[pos++];
    let left: Scalar;
    if (token === '(') {
      left = parse();
      if (tokens[pos++] !== ')') throw new Error('unbalanced condition');
    } else if (token === '!') {
      const value = truth(parse(7)); left = value === undefined ? undefined : !value;
    } else if (token === '-') {
      const value = parse(7); left = value === undefined ? undefined : -Number(value);
    } else if (token?.startsWith('"')) left = JSON.parse(token) as string;
    else if (token === 'true' || token === 'false') left = token === 'true';
    else if (token === 'null') left = null;
    else if (/^\d|^\./.test(token ?? '')) left = Number(token);
    else left = env[token] as Scalar;
    while (pos < tokens.length && (precedence[tokens[pos]] ?? -1) >= min) {
      const op = tokens[pos++];
      let right = parse(precedence[op] + 1);
      if (op === '&&' || op === '||') {
        const a = truth(left), b = truth(right);
        left = op === '&&' ? (a === false || b === false ? false : a === undefined || b === undefined ? undefined : true)
          : (a === true || b === true ? true : a === undefined || b === undefined ? undefined : false);
        continue;
      }
      if (left === undefined || right === undefined) { left = undefined; continue; }
      if (op === '==' || op === '!=') {
        const equal = left === right || (left !== null && right !== null && Number(left) === Number(right));
        left = op === '==' ? equal : !equal;
      } else {
        if (typeof left === 'string' && typeof right === 'string' && left in ranks && right in ranks) {
          left = ranks[left]; right = ranks[right];
        }
        const a = Number(left), b = Number(right);
        left = op === '<' ? a < b : op === '>' ? a > b : op === '<=' ? a <= b : op === '>=' ? a >= b
          : op === '+' ? a + b : op === '-' ? a - b : op === '*' ? a * b : a / b;
      }
    }
    return left;
  }
  try { const result = truth(parse()); return pos === tokens.length ? result : undefined; }
  catch { return undefined; }
}

export function environment(configs: Config[], context: Environment, mode: OptionMode): Environment {
  const merged = Object.assign({}, ...configs) as Config;
  const env: Environment = {};
  for (const [key, def] of Object.entries(OPTIONS)) {
    let v: unknown = merged[key] ?? (def.nullable ? null : def.default);
    if (Array.isArray(v)) v = v[0];
    if (def.nullable && (v === 'nil' || v === '')) v = null;
    if (v && typeof v === 'object' && 'value' in v) v = v.value;
    if (def.type === 'bool' || def.type === 'bools') v = v == null ? null : v === true || v === 1 || v === '1' || v === 'true';
    else if (/^(float|int|percent)/.test(def.type) && typeof v === 'string') v = Number(v.replace(/%$/, '').split(',')[0]);
    env[key] = v;
  }
  return { ...env, ...context, mode: mode === 'simple' ? 'Simple' : mode === 'advanced' ? 'Advanced' : mode === 'developer' ? 'Develop' : 'Expert' };
}

export function optionState(desc: TabDesc, env: Environment): Record<string, { visible: boolean; enabled: boolean }> {
  const states: Record<string, { visible: boolean; enabled: boolean }> = {};
  for (const rule of desc.visibility ?? []) {
    if (rule.when && condition(rule.when, env) !== true) continue;
    const value = condition(rule.condition, env);
    if (value === undefined) continue;
    for (const key of rule.options) {
      const base = splitKey(key)[0];
      (states[base] ??= { visible: true, enabled: true })[rule.effect] = value;
    }
  }
  if (env.support_style === 'native') (states.support_on_build_plate_only ??= { visible: true, enabled: true }).enabled = false;
  return states;
}

export function fieldDefinition(key: string, env: Environment): Partial<OptionDef> {
  const def = OPTIONS[key];
  if (!def) return {};
  let values: string[] | undefined;
  if (key === 'support_style') values = [...(String(env.support_type).startsWith('tree') ? ['default', 'tree_slim', 'tree_strong', 'tree_hybrid', 'organic'] : ['default', 'grid', 'snug']),
    ...(def.enum_values?.includes('native') && (env.native_support_capable || env.support_style === 'native') ? ['native'] : [])];
  if (key === 'brim_type') values = def.enum_values?.filter((v) => v !== 'leading_edge_only' || env.belt_printer || env.brim_type === v);
  if (key === 'wipe_tower_wall_type') values = def.enum_values?.filter((v) => v !== 'cone' || env.printer_vendor !== 'BBL');
  if (key === 'input_shaping_type') values = env.gcode_flavor === 'klipper'
    ? ['Default', 'ZV', 'MZV', 'ZVD', 'EI', '2HUMP_EI', '3HUMP_EI', 'Disable']
    : env.gcode_flavor === 'reprapfirmware'
      ? ['Default', 'MZV', 'ZVD', 'ZVDD', 'ZVDDD', 'EI2', 'EI3', 'DAA', 'Disable']
      : env.gcode_flavor === 'marlin2' ? ['ZV', 'Disable'] : ['Default', 'Disable'];
  if (values) return { enum_values: values, enum_labels: values.map((v) => def.enum_labels?.[def.enum_values!.indexOf(v)] ?? v) };
  if (key === 'nonplanar_mode') {
    const all = [...(def.enum_values ?? []).filter((v) => v !== 'oneshot'), ...(canOneShot() && (env.mode !== 'Simple' || env.nonplanar_mode === 'oneshot') ? ['oneshot'] : [])];
    const rotary = String(env.nonplanar_kinematics ?? 'none') !== 'none';
    return {
      label: 'Slicing method',
      enum_values: all,
      enum_labels: all.map((v, i) => `${v === 'oneshot' ? 'One-shot' : def.enum_labels?.[i] ?? v}${!rotary && ['multiplanar', 'field'].includes(v) ? ' (needs rotary axes)' : ''}`),
    };
  }
  if (key === 'brim_width' && env.brim_type === 'brim_ears' && condition('belt_printer == true && (belt_slice_rotation == "x" || belt_slice_rotation == "y") && (belt_slice_rotation_angle >= 1 && belt_slice_rotation_angle <= 85 || belt_slice_rotation_angle <= -1 && belt_slice_rotation_angle >= -85)', env) !== true)
    return { label: 'Brim ear radius' };
  return {};
}
