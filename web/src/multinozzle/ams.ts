import type { Tray } from '../printers/connections';

export interface SendFilament { type: string; color: string; extruder?: number; name?: string; vendor?: string; id?: string; requiredHRC?: number; printable?: number; compatibility?: number;
  flow?: string; diameter?: number; hasFilamentSwitch?: boolean; object?: boolean; support?: boolean }
export const isExternalTray = (id: number) => id === 254 || id === 255;
export const availableTrays = (filament: SendFilament, trays: Tray[]) => trays.filter((t) => filament.extruder === undefined || t.extruder === undefined || t.extruder === filament.extruder);
export const trayLabel = (t: Tray) => (isExternalTray(t.id) ? t.id === 254 ? 'Left external spool' : 'External spool' :
  t.kind === 'ams-ht' ? `AMS HT ${t.ams - 127}` : `AMS ${t.ams + 1} slot ${t.slot + 1}`) + (t.type ? ` · ${t.type}` : ' · empty');
const rgb = (c: string) => [1, 3, 5].map((i) => parseInt(c.slice(i, i + 2), 16) || 0);
const distance = (a: string, b: string) => rgb(a).reduce((d, v, i) => d + (v - rgb(b)[i]) ** 2, 0);

export function autoMap(filaments: SendFilament[], trays: Tray[]): number[] {
  const used = new Set<number>();
  return filaments.map((f) => {
    const available = availableTrays(f, trays);
    const same = available.filter((t) => t.type && t.type.toUpperCase() === f.type.toUpperCase())
      .sort((a, b) => Number(used.has(a.id)) - Number(used.has(b.id)) || distance(a.color, f.color) - distance(b.color, f.color));
    const pick = same[0];
    if (pick) used.add(pick.id);
    return pick?.id ?? -1;
  });
}

export function physicalMapping(ids: number[], trays: Tray[]) {
  return ids.map((id) => {
    const tray = trays.find((t) => t.id === id);
    if (!tray) throw new Error('Choose a reported AMS tray or external spool for every filament.');
    return { ams_id: isExternalTray(id) ? id : tray.ams, slot_id: isExternalTray(id) ? 0 : tray.slot };
  });
}
