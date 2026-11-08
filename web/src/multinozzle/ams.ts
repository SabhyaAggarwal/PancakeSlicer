import type { Tray } from '../printers/connections';
import { bambuFilamentId } from './material-checks';

export interface SendFilament { key?: string; type: string; color: string; extruder?: number; name?: string; vendor?: string; id?: string; requiredHRC?: number; printable?: number; compatibility?: number;
  flow?: string; diameter?: number; hasFilamentSwitch?: boolean; object?: boolean; support?: boolean }
export const isExternalTray = (id: number) => id === 254 || id === 255;
export const availableTrays = (filament: SendFilament, trays: Tray[]) => trays.filter((t) => t.type && (filament.extruder === undefined || t.extruder === undefined || t.extruder === filament.extruder));
export function trayCode(t: Tray, trays: readonly Tray[]): string {
  if (isExternalTray(t.id)) {
    const sides = trays.filter((x) => isExternalTray(x.id)).length > 1;
    return !sides ? 'External spool' : t.extruder === 0 || (t.extruder === undefined && t.id === 254) ? 'Left external spool' : 'Right external spool';
  }
  if (t.kind === 'ams-ht') return `AMS HT ${t.ams - 127}`;
  const units = new Set(trays.filter((x) => !isExternalTray(x.id) && x.kind !== 'ams-ht').map((x) => x.ams)).size;
  return units > 1 ? `AMS ${t.ams + 1} slot ${t.slot + 1}` : `Slot ${t.slot + 1}`;
}
export const trayFilament = (t: Tray) => (!isExternalTray(t.id) && t.name) || t.type || 'Empty';

const lab = (hex: string) => {
  const [r, g, b] = [1, 3, 5].map((i) => { const c = (parseInt(hex.slice(i, i + 2), 16) || 0) / 255; return c > 0.04045 ? ((c + 0.055) / 1.055) ** 2.4 : c / 12.92; });
  const f = (t: number) => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
  const [x, y, z] = [(0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.9505, 0.2126 * r + 0.7152 * g + 0.0722 * b, (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.089].map(f);
  return [116 * y - 16, 500 * (x - y), 200 * (y - z)];
};
const deltaE = (a: string, b: string) => { const p = lab(a), q = lab(b); return Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]); };

export function autoMap(filaments: SendFilament[], trays: Tray[], mapped: (t: Tray) => string | null | undefined = () => undefined): number[] {
  const used = new Set<number>();
  return filaments.map((f) => {
    const available = availableTrays(f, trays);
    const id = f.id && bambuFilamentId(f.id);
    const same = f.key ? available.filter((t) => mapped(t) === f.key) : [];
    const sameId = same.length ? same : id ? available.filter((t) => t.filament_id && bambuFilamentId(t.filament_id) === id) : [];
    const candidates = sameId.length ? sameId : available.filter((t) => t.type.toUpperCase() === f.type.toUpperCase());
    const pick = [...candidates].sort((a, b) => Number(used.has(a.id)) - Number(used.has(b.id)) || deltaE(a.color, f.color) - deltaE(b.color, f.color))[0];
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
