export type PresetType = 'machine' | 'process' | 'filament';
export type ConfigValue = string | number | boolean | string[] | number[];
export type Config = Record<string, ConfigValue>;

export interface SystemPreset {
  name: string;
  vendor: string;
  type: PresetType;
  compatible_printers?: string[];
  config: Config;
  model?: string;
  variant?: string;
  bed_model?: string;
  bed_texture?: string;
  default_bed_type?: string;
  not_support_bed_type?: string[];
  default_materials?: string[];
}

export interface UserPreset {
  name: string;
  type: PresetType;
  inherits: string;
  overrides: Config;
}

export interface PresetCatalog {
  machines: SystemPreset[];
  processes: SystemPreset[];
  filaments: SystemPreset[];
}

export type JobState = 'queued' | 'running' | 'done' | 'failed' | 'canceled';

export interface JobStatus {
  id: string;
  state: JobState;
  progress?: number;
  stage?: string;
  error?: string;
  record?: Record<string, unknown>;
  output?: 'gcode' | 'gcode.3mf' | '3mf';
  filename?: string;
}

export interface ProgressEvent {
  stage: string;
  pct: number;
  message?: string;
}

export interface PlateJson {
  objects: {
    file: string;
    name: string;
    extruder?: number;
    instances: { x: number; y: number; rot: number; z?: number; printable?: boolean }[];
    settings?: Record<string, string>;
  }[];
}

export interface JobRequest {
  files: { name: string; blob: Blob }[];
  vendor: string;
  printer: string;
  process: string;
  filaments: string[];
  overrides: Config;
  plate?: PlateJson;
  project_instances?: { printable: boolean }[];
  events: string[];
  output: 'gcode' | 'gcode.3mf' | '3mf';
}
