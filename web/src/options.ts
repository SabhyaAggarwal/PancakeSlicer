import optionsJson from './options.generated.json';
import processTab from './tabs/process.json';
import filamentTab from './tabs/filament.json';
import printerTab from './tabs/printer.json';

export type OptionMode = 'simple' | 'advanced' | 'develop';

export interface OptionDef {
  key: string;
  type: string;
  label?: string;
  full_label?: string;
  category?: string;
  tooltip?: string;
  sidetext?: string;
  min?: number;
  max?: number;
  mode: OptionMode;
  enum_values?: string[];
  enum_labels?: string[];
  default?: unknown;
  readonly?: boolean;
  gui_type?: string;
  multiline?: boolean;
  full_width?: boolean;
}

export interface TabGroup {
  title: string;
  icon?: string;
  options: string[];
}
export interface TabPage {
  title: string;
  icon: string;
  groups: TabGroup[];
}
export interface TabDesc {
  tab: 'process' | 'filament' | 'printer';
  pages: TabPage[];
  removed_keys: string[];
}

const INVENTORY_MODES: Record<string, OptionMode> = {
  Simple: 'simple', Advanced: 'advanced', Expert: 'develop', Develop: 'develop',
};
export const OPTIONS: Record<string, OptionDef> = Object.fromEntries(
  Object.entries(optionsJson).map(([key, def]) => [key, {
    ...def,
    enum_values: def.enum_values.filter((value): value is string => typeof value === 'string'),
    type: def.type.replace(/Or/g, '_or_').toLowerCase(),
    mode: INVENTORY_MODES[def.mode],
  }]),
);
export const TABS: Record<TabDesc['tab'], TabDesc> = {
  process: processTab as TabDesc,
  filament: filamentTab as TabDesc,
  printer: printerTab as TabDesc,
};

export function splitKey(k: string): [string, number] {
  const [key, idx] = k.split('#');
  return [key, idx === undefined ? 0 : Number(idx)];
}

const MODE_RANK: Record<OptionMode, number> = { simple: 0, advanced: 1, develop: 2 };
export const modeVisible = (opt: OptionMode, ui: OptionMode) => MODE_RANK[opt] <= MODE_RANK[ui];

const HIDDEN = /print_host|printhost|printer_agent|host_type|api_key|bbl_use_printhost|upload|cloud|device|octoprint|_plugin_config_overrides/;
const REMOVED_KEYS = new Set(Object.values(TABS).flatMap((tab) => tab.removed_keys));
export const isHidden = (key: string) => REMOVED_KEYS.has(key) || HIDDEN.test(key);
