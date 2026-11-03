import { useEffect, useState } from 'react';
import type { Config, ConfigValue, PresetType } from '../api/types';
import { normalizeConfigValue, toolValue } from '../config-values';
import { loadTab, OPTIONS, splitKey, TABS, isHidden } from '../options';
import { display } from '../profiles/format';

export { display };

const TAB = { machine: 'printer', filament: 'filament', process: 'process' } as const;

export interface DiffRow { key: string; label: string; group: string; left?: ConfigValue; right?: ConfigValue; differs: boolean }

export function diffRows(type: PresetType, left: Config, right: Config, all = false): DiffRow[] {
  const rows: DiffRow[] = [];
  const seen = new Set<string>();
  const differs = (k: string) => {
    const a = left[k] ?? (OPTIONS[k]?.default as ConfigValue | undefined), b = right[k] ?? (OPTIONS[k]?.default as ConfigValue | undefined);
    return (a === undefined ? '' : toolValue(k, normalizeConfigValue(k, a))) !== (b === undefined ? '' : toolValue(k, normalizeConfigValue(k, b)));
  };
  for (const page of TABS[TAB[type]]?.pages ?? []) for (const group of page.groups) for (const line of group.lines) for (const k of line.options) {
    const key = splitKey(k)[0];
    if (seen.has(key) || !OPTIONS[key] || isHidden(key)) continue;
    seen.add(key);
    const d = differs(key);
    if (d || all) rows.push({ key, label: OPTIONS[key].label || line.label || key, group: `${page.title} › ${group.title}`, left: left[key], right: right[key], differs: d });
  }
  for (const key of new Set([...Object.keys(left), ...Object.keys(right)]))
    if (!seen.has(key) && OPTIONS[key] && !isHidden(key) && differs(key))
      rows.push({ key, label: OPTIONS[key].label || key, group: 'Other', left: left[key], right: right[key], differs: true });
  return rows;
}

export const useTabLoaded = (type: PresetType) => {
  const [ready, setReady] = useState(!!TABS[TAB[type]]);
  useEffect(() => void loadTab(TAB[type]).then(() => setReady(true), () => setReady(true)), [type]);
  return ready;
};
