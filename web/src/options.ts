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
  icon: string;
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
}

export const OPTIONS = optionsJson as unknown as Record<string, OptionDef>;
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
export const isHidden = (key: string) => HIDDEN.test(key);
