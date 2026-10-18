import { vectors as shippedVectors } from 'virtual:option-bootstrap';
import type { Config, ConfigValue } from './api/types';

const vectors: Record<string, string> = { ...shippedVectors };
const vectorTypes = ['Floats', 'Ints', 'Percents', 'Bools', 'FloatsOrPercents', 'Points', 'Enums', 'Strings', 'PointsGroups', 'IntsGroups'];
export function vectorType(type: string): string | undefined {
  const name = type.replace(/^co/, '').replace(/_/g, '').toLowerCase();
  return vectorTypes.find((t) => t.toLowerCase() === name);
}

export function setOptionTypes(options: Record<string, { type: string }>) {
  for (const key of Object.keys(vectors)) delete vectors[key];
  for (const [key, def] of Object.entries(options)) {
    const type = vectorType(def.type);
    if (type) vectors[key] = type;
  }
}

export const unescapeConfigString = (text: string) => text.replace(/\\([\s\S])/g, (_, c: string) => c === 'n' ? '\n' : c === 'r' ? '\r' : c);

function stringVector(text: string): string[] {
  const result: string[] = [];
  let i = 0;
  while (i < text.length) {
    while (text[i] === ' ' || text[i] === '\t') i++;
    if (i === text.length) break;
    let word = '';
    if (text[i] === '"') {
      i++;
      while (i < text.length && text[i] !== '"') {
        let c = text[i++];
        if (c === '\\') {
          if (i === text.length) throw new Error('Incomplete string vector escape');
          c = text[i++];
          if (c === 'n') c = '\n';
          else if (c === 'r') c = '\r';
        }
        word += c;
      }
      if (text[i++] !== '"') throw new Error('Unclosed string vector quote');
    } else {
      while (i < text.length && text[i] !== ';') word += text[i++];
    }
    result.push(word);
    while (text[i] === ' ' || text[i] === '\t') i++;
    if (i === text.length) break;
    if (text[i++] !== ';') throw new Error('Expected string vector separator');
    if (i === text.length) result.push('');
  }
  return result;
}

const scalar = (v: unknown): string => {
  if (v == null) return 'nil';
  if (v === true) return '1';
  if (v === false) return '0';
  if (typeof v === 'object' && 'value' in v) return `${v.value}${'percent' in v && v.percent ? '%' : ''}`;
  return String(v);
};

export function normalizeConfigValue(key: string, value: ConfigValue, type = vectors[key]): ConfigValue {
  if (!type) return value;
  let values: unknown[];
  if (Array.isArray(value)) values = value;
  else if (type === 'Strings') values = stringVector(String(value));
  else values = value === '' ? [] : String(value).split((type === 'PointsGroups' || type === 'IntsGroups') ? '#' : ',');
  return values.map((v) => {
    if (type === 'Points' && Array.isArray(v)) return v.map(scalar).join('x');
    const text = scalar(v);
    if (type === 'Points') return text.split('x').map((part) => part.trim() !== '' && Number.isFinite(Number(part)) ? String(Number(part)) : part).join('x');
    if (type === 'Strings' || type === 'Enums' || type === 'PointsGroups' || type === 'IntsGroups') return text;
    const trimmed = text.trim();
    if (type === 'Bools' && /^(true|false)$/.test(trimmed)) return trimmed === 'true' ? '1' : '0';
    const hasPercent = trimmed.endsWith('%');
    const percent = hasPercent && type === 'FloatsOrPercents' ? '%' : '';
    const number = Number(hasPercent ? trimmed.slice(0, -1) : trimmed);
    return trimmed !== '' && Number.isFinite(number) ? `${number}${percent}` : trimmed;
  });
}

export function normalizeConfig(config: Config): Config {
  return Object.fromEntries(Object.entries(config).map(([key, value]) => [key, normalizeConfigValue(key, value)]));
}

export function toolValue(key: string, value: ConfigValue): string {
  if (!Array.isArray(value)) return scalar(value);
  const parts = value.map(scalar);
  if (vectors[key] === 'Strings' || !vectors[key]) return parts.map((text) =>
    (parts.length === 1 && text === '') || /[ \t;\\"\r\n]/.test(text)
      ? `"${text.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\r/g, '\\r').replace(/\n/g, '\\n')}"` : text).join(';');
  return parts.join((vectors[key] === 'PointsGroups' || vectors[key] === 'IntsGroups') ? '#' : ',');
}

export function jobValue(key: string, value: ConfigValue): ConfigValue {
  return Array.isArray(value) && vectors[key] && vectors[key] !== 'Strings' ? toolValue(key, value) : value;
}
