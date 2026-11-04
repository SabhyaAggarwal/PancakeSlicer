declare module 'virtual:bambu-checks' {
  export interface MaterialRule {
    action: string; description: string; wiki?: string;
    vendor?: string; type?: string; types?: string[]; type_suffix?: string; name?: string; name_suffix?: string;
    slot?: string; model_id?: string[]; extruder_id?: number[]; nozzle_flows?: string[]; nozzle_diameters?: number[];
    calib_mode?: string; has_filament_switch?: boolean; used_for_print_support?: boolean; used_for_print_object?: boolean;
    white_names?: string[]; white_fila_ids?: string[];
  }
  export const rules: MaterialRule[];
  export const hardness: Record<string, number>;
  export const modelIds: Record<string, string>;
  export const filamentIds: Record<string, string>;
}
