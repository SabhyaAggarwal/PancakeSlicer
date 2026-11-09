import { useEffect, useState } from 'react';
import type { UserPreset } from '../api/types';
import { responseError } from '../api/access';
import { printFromV4, type V4Record } from '../persist/profile-library';
import { getState, type State } from '../state/store';
import { effectiveProfileValues } from './context';
import { profileKey } from './identity';
import { restoreArchivedProfile } from './library-actions';
import { commitProfileRecords, profileCopyCreation, profileCreation } from './transactions';

export interface PrintMaster { id: string; name: string; description: string; layers: [number, number]; record: V4Record }

let read: Promise<PrintMaster[]> | null = null;
export function printMasters(): Promise<PrintMaster[]> {
  read ??= fetch('/api/print-catalog').then(async (r) => { if (!r.ok) throw await responseError(r); return await r.json() as PrintMaster[]; })
    .catch((e) => { read = null; throw e; });
  return read;
}
export function usePrintMasters(enabled: boolean): { data?: PrintMaster[]; error?: string } {
  const [out, setOut] = useState<{ data?: PrintMaster[]; error?: string }>({});
  useEffect(() => {
    if (!enabled || out.data) return;
    let live = true;
    printMasters().then((data) => { if (live) setOut({ data }); }, (e) => { if (live) setOut({ error: e instanceof Error ? e.message : String(e) }); });
    return () => { live = false; };
  }, [enabled, out.data]);
  return out;
}

const round2 = (n: number) => Math.round(n * 100) / 100;
export function layerRange(s: State, m: PrintMaster): string {
  const nozzle = Number([effectiveProfileValues(s, 'machine', s.printer).nozzle_diameter].flat()[0]) || 0.4;
  return `${round2(m.layers[0] * nozzle).toFixed(2)}–${round2(m.layers[1] * nozzle).toFixed(2)} mm`;
}

const printProfiles = (s: State) => s.userPresets.filter((p) => p.type === 'process');
export const isAdded = (s: State, m: PrintMaster) => printProfiles(s).some((p) => profileKey(p) === m.id && !p.archived);
export function freeName(s: State, name: string): string {
  const taken = new Set(printProfiles(s).map((p) => p.name));
  let out = name.trim();
  for (let n = 2; taken.has(out); n++) out = `${name.trim()} ${n}`;
  return out;
}

export async function addPrintMaster(m: PrintMaster): Promise<string> {
  const prior = printProfiles(getState()).find((p) => profileKey(p) === m.id);
  if (prior?.archived) { await commitProfileRecords([restoreArchivedProfile(prior)]); return m.id; }
  if (prior) return m.id;
  return profileCreation((s) => printProfiles(s).some((p) => profileKey(p) === m.id) ? m.id
    : { ...printFromV4({ ...m.record, revision: 0 }), name: freeName(s, m.name) } as UserPreset).commit();
}

export function createPrintProfile(base: { profile: UserPreset } | { master: PrintMaster }, name: string): Promise<string> {
  if ('profile' in base) return profileCopyCreation(base.profile, name.trim()).commit();
  return profileCreation((s) => {
    if (printProfiles(s).some((p) => p.name === name.trim())) throw new Error('You already have a profile with that name—try a different one!');
    return { ...printFromV4({ ...base.master.record, id: `profile:${crypto.randomUUID()}`, revision: 0 }), name: name.trim() } as UserPreset;
  }).commit();
}
