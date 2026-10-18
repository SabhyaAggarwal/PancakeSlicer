import type { OptionDef, OptionMode } from './options';

const MODES: Record<string, OptionMode> = {
  Simple: 'simple', Advanced: 'advanced', Expert: 'develop', Develop: 'develop',
};

export function normalizeOptions(inventory: Record<string, { type: string; mode: string; enum_values: unknown[] }>): Record<string, OptionDef> {
  return Object.fromEntries(Object.entries(inventory).map(([key, def]) => [key, {
    ...def,
    key,
    enum_values: def.enum_values.filter((value): value is string => typeof value === 'string'),
    type: def.type.replace(/Or/g, '_or_').toLowerCase(),
    mode: MODES[def.mode],
  }]));
}
