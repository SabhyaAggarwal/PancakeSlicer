import { strFromU8, strToU8, unzipSync, zipSync } from 'fflate';
import type { State } from '../state/store';

export const projectProfileIds = (s: Pick<State, 'printer' | 'process' | 'filamentSlots'>) => ({
  printer_settings_id: s.printer,
  print_settings_id: s.process,
  filament_settings_id: s.filamentSlots,
});

export async function stampProjectProfiles(blob: Blob, s: Pick<State, 'printer' | 'process' | 'filamentSlots'>): Promise<Blob> {
  const files = unzipSync(new Uint8Array(await blob.arrayBuffer()));
  const path = Object.keys(files).find((p) => p.toLowerCase() === 'metadata/project_settings.config');
  if (!path) return blob;
  const config = JSON.parse(strFromU8(files[path]));
  files[path] = strToU8(JSON.stringify({ ...config, ...projectProfileIds(s) }));
  return new Blob([zipSync(files) as BlobPart], { type: blob.type });
}
